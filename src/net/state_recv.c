/* ============================================================
 * @deps-implements: net/state_recv.h
 * @deps-requires: net/state_recv.h, net/protocol.h (NetPlayerView.rogue/duel_revealed_card),
 *                 core/game_state.h (GameState, GamePhase, PassDirection, NUM_PLAYERS),
 *                 core/hand.h (hand_init), core/card.h (Card, Suit, CARDS_PER_TRICK),
 *                 core/trick.h (Trick), phase2/phase2_state.h (Phase2State, PlayerPhase2),
 *                 phase2/contract.h (ContractInstance),
 *                 phase2/transmutation.h (TransmuteInventory, HandTransmuteState),
 *                 phase2/effect.h (ActiveEffect, EffectScope, Effect, MAX_ACTIVE_EFFECTS),
 *                 string.h, stdio.h
 * @deps-last-changed: 2026-04-04 — Updated to apply NetPlayerView rogue/duel revealed card fields
 * ============================================================ */

#include "state_recv.h"

#include <stdio.h>
#include <string.h>

#include "net/protocol.h"
#include "core/game_state.h"
#include "core/hand.h"
#include "core/card.h"
#include "core/trick.h"
#include "phase2/phase2_state.h"
#include "phase2/contract.h"
#include "phase2/transmutation.h"
#include "phase2/effect.h"

/* ================================================================
 * Seat remapping: server seat → local player index
 *
 * Local player 0 = us (my_seat on server).
 * Formula: local = (server_seat - my_seat + 4) % 4
 * Reverse: server_seat = (local + my_seat) % 4
 * ================================================================ */

static inline int remap_seat(int server_seat, int my_seat)
{
    if (server_seat < 0) return server_seat; /* preserve -1 sentinels */
    return (server_seat - my_seat + NUM_PLAYERS) % NUM_PLAYERS;
}

/* ================================================================
 * Phase 2 conversion helpers
 * ================================================================ */

static void apply_contract(ContractInstance *dst, const NetContractView *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->contract_id = src->contract_id;
    dst->revealed    = src->revealed;
    dst->completed   = src->completed;
    dst->failed      = src->failed;
    dst->tricks_won  = src->tricks_won;
    dst->points_taken = src->points_taken;
    for (int s = 0; s < SUIT_COUNT; s++)
        dst->cards_collected[s] = src->cards_collected[s];
    dst->tricks_won_mask = src->tricks_won_mask;
    dst->max_streak      = src->max_streak;
    dst->paired_transmutation_id = src->paired_transmutation_id;
}

static void apply_transmute_slot(TransmuteSlot *dst,
                                 const NetTransmuteSlotView *src,
                                 int my_seat)
{
    dst->transmutation_id  = src->transmutation_id;
    dst->original_card     = net_card_to_game(src->original_card);
    dst->transmuter_player = remap_seat(src->transmuter_player, my_seat);
    dst->fogged            = src->fogged;
    dst->fog_transmuter    = remap_seat(src->fog_transmuter, my_seat);
}

static void apply_draft_player(DraftPlayerState *dst,
                               const NetDraftPlayerView *src)
{
    dst->available_count = src->available_count;
    for (int i = 0; i < DRAFT_GROUP_SIZE; i++) {
        dst->available[i].contract_id      = src->available[i].contract_id;
        dst->available[i].transmutation_id = src->available[i].transmutation_id;
    }
    dst->pick_count = src->pick_count;
    for (int i = 0; i < DRAFT_PICKS_PER_PLAYER; i++) {
        dst->picked[i].contract_id      = src->picked[i].contract_id;
        dst->picked[i].transmutation_id = src->picked[i].transmutation_id;
    }
    dst->has_picked_this_round = src->has_picked_this_round;
}

/* ================================================================
 * Public API
 * ================================================================ */

void state_recv_apply(GameState *gs, Phase2State *p2,
                      const NetPlayerView *view, bool defer_trick)
{
    int my = view->my_seat;

    /* ---- 1. Phase & round state ---- */
    GamePhase server_phase = (GamePhase)view->phase;

    /* Always apply server phase. Pass animations are not played online
     * because the server doesn't send opponent pass selections needed
     * for the toss/reveal visual pipeline. Future: add animation hints
     * from server to enable pass visuals. */
    gs->phase = server_phase;
    gs->round_number   = view->round_number;
    gs->pass_direction = (PassDirection)view->pass_direction;
    gs->pass_card_count = view->pass_card_count;
    /* lead_player is deferred alongside trick data (below) to keep
     * game_state_current_player() consistent during animations.
     * Applying new lead_player with stale num_played produces a
     * wrong current-player value. */
    gs->hearts_broken  = view->hearts_broken;
    gs->tricks_played  = view->tricks_played;

    /* ---- 2. Own hand (full card identities) ---- */
    /* Save old hand order to preserve manual arrangement after rebuild */
    Card old_order[MAX_HAND_SIZE];
    int old_count = gs->players[0].hand.count;
    for (int i = 0; i < old_count; i++)
        old_order[i] = gs->players[0].hand.cards[i];

    hand_init(&gs->players[0].hand);
    for (int i = 0; i < view->hand_count && i < MAX_HAND_SIZE; i++) {
        Card c = net_card_to_game(view->hand[i]);
        hand_add_card(&gs->players[0].hand, c);
    }

    /* Reorder new hand to match player's manual card arrangement.
     * Cards that existed before keep their relative position;
     * new cards (from pass exchange) are appended at the end.
     * reorder_map[new_pos] = server_pos — used later to reorder
     * transmute slots to stay in sync with the hand. */
    int reorder_map[MAX_HAND_SIZE];
    for (int i = 0; i < MAX_HAND_SIZE; i++)
        reorder_map[i] = i; /* identity by default */

    if (old_count > 0 && gs->players[0].hand.count > 0) {
        Card reordered[MAX_HAND_SIZE];
        bool used[MAX_HAND_SIZE] = {false};
        int out = 0;

        for (int o = 0; o < old_count; o++) {
            for (int n = 0; n < gs->players[0].hand.count; n++) {
                if (!used[n] && card_equals(gs->players[0].hand.cards[n],
                                            old_order[o])) {
                    reordered[out] = gs->players[0].hand.cards[n];
                    reorder_map[out] = n;
                    out++;
                    used[n] = true;
                    break;
                }
            }
        }
        for (int n = 0; n < gs->players[0].hand.count; n++) {
            if (!used[n]) {
                reordered[out] = gs->players[0].hand.cards[n];
                reorder_map[out] = n;
                out++;
            }
        }
        /* out == hand.count: every new card is either matched or appended */
        for (int i = 0; i < out; i++)
            gs->players[0].hand.cards[i] = reordered[i];
    }

    /* ---- 3. All players: hand counts + scores (remapped) ---- */
    for (int s = 0; s < NUM_PLAYERS; s++) {
        int local = remap_seat(s, my);
        /* Don't overwrite player 0's hand cards — only counts for opponents */
        if (local != 0)
            gs->players[local].hand.count = view->hand_counts[s];
        gs->players[local].round_points = view->round_points[s];
        gs->players[local].total_score  = view->total_scores[s];
    }

    /* ---- 4. Current trick (remapped player IDs) ---- */
    /* Skip during trick animations — the saved_trick snapshot is
     * authoritative and overwriting here would corrupt animation state. */
    if (!defer_trick) {
        gs->lead_player = remap_seat(view->lead_player, my);
        gs->current_trick.num_played  = view->current_trick.num_played;
        gs->current_trick.lead_player = remap_seat(view->current_trick.lead_player, my);
        gs->current_trick.lead_suit   = (Suit)view->current_trick.lead_suit;
        for (int i = 0; i < CARDS_PER_TRICK; i++) {
            if (i < (int)view->current_trick.num_played) {
                gs->current_trick.cards[i] =
                    net_card_to_game(view->current_trick.cards[i]);
                gs->current_trick.player_ids[i] =
                    remap_seat(view->current_trick.player_ids[i], my);
            } else {
                gs->current_trick.cards[i] = (Card){.suit = -1, .rank = -1};
                gs->current_trick.player_ids[i] = -1;
            }
        }
    }

    /* ---- 5. Pass state (remapped) ---- */
    for (int s = 0; s < NUM_PLAYERS; s++) {
        int local = remap_seat(s, my);
        gs->pass_ready[local] = view->pass_ready[s];
    }
    /* Own pass selections */
    for (int i = 0; i < MAX_PASS_CARD_COUNT; i++) {
        if (i < (int)view->pass_card_count)
            gs->pass_selections[0][i] =
                net_card_to_game(view->my_pass_selections[i]);
        else
            gs->pass_selections[0][i] = (Card){.suit = -1, .rank = -1};
    }

    /* ---- Phase 2 ---- */
    if (!view->phase2_enabled) {
        p2->enabled = false;
        return;
    }
    p2->enabled = true;

    /* ---- 6. Own contracts (player 0) ---- */
    int nc = view->my_num_contracts;
    if (nc > MAX_ACTIVE_CONTRACTS) nc = MAX_ACTIVE_CONTRACTS;
    p2->players[0].num_active_contracts = nc;
    for (int i = 0; i < MAX_ACTIVE_CONTRACTS; i++) {
        if (i < nc)
            apply_contract(&p2->players[0].contracts[i],
                           &view->my_contracts[i]);
        else
            p2->players[0].contracts[i].contract_id = -1;
    }

    /* ---- 6b. Opponent contracts (remapped) ---- */
    for (int s = 0; s < NUM_PLAYERS; s++) {
        int local = remap_seat(s, my);
        if (local == 0) continue; /* player 0 handled above */
        const NetOpponentContracts *oc = &view->opponent_contracts[s];
        int onc = oc->num_contracts;
        if (onc > MAX_ACTIVE_CONTRACTS) onc = MAX_ACTIVE_CONTRACTS;
        p2->players[local].num_active_contracts = onc;
        for (int i = 0; i < MAX_ACTIVE_CONTRACTS; i++) {
            if (i < onc) {
                p2->players[local].contracts[i].contract_id = oc->contract_ids[i];
                p2->players[local].contracts[i].completed   = oc->completed[i];
                p2->players[local].contracts[i].paired_transmutation_id = -1;
            } else {
                p2->players[local].contracts[i].contract_id = -1;
            }
        }
    }

    /* ---- 7. Transmutation inventory (player 0) ---- */
    int inv_count = view->transmute_inv_count;
    if (inv_count > MAX_TRANSMUTE_INVENTORY) inv_count = MAX_TRANSMUTE_INVENTORY;
    p2->players[0].transmute_inv.count = inv_count;
    for (int i = 0; i < MAX_TRANSMUTE_INVENTORY; i++) {
        if (i < inv_count)
            p2->players[0].transmute_inv.items[i] = view->transmute_inv[i];
        else
            p2->players[0].transmute_inv.items[i] = -1;
    }

    /* ---- 8. Hand transmute state (player 0, reordered to match hand) ---- */
    for (int i = 0; i < MAX_HAND_SIZE; i++) {
        int src = reorder_map[i]; /* server-side index for this hand slot */
        if (i < view->hand_count && src < view->hand_count)
            apply_transmute_slot(&p2->players[0].hand_transmutes.slots[i],
                                 &view->hand_transmutes[src], my);
        else {
            p2->players[0].hand_transmutes.slots[i].transmutation_id = -1;
            p2->players[0].hand_transmutes.slots[i].fogged = false;
        }
    }

    /* ---- 9. Draft state (player 0 + round-level) ---- */
    p2->round.draft.active        = view->draft_active;
    p2->round.draft.current_round = view->draft_current_round;
    apply_draft_player(&p2->round.draft.players[0], &view->my_draft);

    /* ---- 10. Game-scoped flags (all players, remapped) ---- */
    for (int s = 0; s < NUM_PLAYERS; s++) {
        int local = remap_seat(s, my);
        p2->shield_tricks_remaining[local] = view->shield_tricks_remaining[s];
        p2->curse_force_hearts[local]      = view->curse_force_hearts[s];
        p2->anchor_force_suit[local]       = view->anchor_force_suit[s];
        p2->binding_auto_win[local]        = view->binding_auto_win[s];
    }

    /* ---- 10b. Mirror history (game-scoped, no remapping needed) ---- */
    p2->last_played_transmute_id = view->last_played_transmute_id;
    p2->last_played_resolved_effect =
        (TransmuteEffect)view->last_played_resolved_effect;
    p2->last_played_transmuted_card = (Card){
        (Suit)view->last_played_transmuted_card_suit,
        (Rank)view->last_played_transmuted_card_rank
    };

    /* ---- 11. Persistent effects (distribute across players, remapped) ---- */
    /* Clear all first */
    for (int p = 0; p < NUM_PLAYERS; p++)
        p2->players[p].num_persistent = 0;

    int eff_count = view->num_persistent_effects;
    if (eff_count > NET_MAX_PLAYERS * NET_MAX_EFFECTS)
        eff_count = NET_MAX_PLAYERS * NET_MAX_EFFECTS;

    for (int i = 0; i < eff_count; i++) {
        const NetActiveEffectView *ne = &view->persistent_effects[i];
        int source_local = remap_seat(ne->source_player, my);

        if (source_local < 0 || source_local >= NUM_PLAYERS) continue;
        PlayerPhase2 *pp = &p2->players[source_local];
        if (pp->num_persistent >= MAX_ACTIVE_EFFECTS) continue;

        ActiveEffect *ae = &pp->persistent_effects[pp->num_persistent++];
        ae->effect.type = (EffectType)ne->effect_type;
        /* All Effect.param union variants are int-sized at offset 0,
         * so writing points_delta covers voided_suit and pass_direction too. */
        ae->effect.param.points_delta = ne->param_value;
        ae->scope = (EffectScope)ne->scope;
        ae->source_player = source_local;
        ae->target_player = (ne->target_player >= 0)
            ? remap_seat(ne->target_player, my) : -1;
        ae->rounds_remaining = ne->rounds_remaining;
        ae->active = ne->active;
    }

    /* ---- 12. Previous round points (remapped) ---- */
    for (int s = 0; s < NUM_PLAYERS; s++) {
        int local = remap_seat(s, my);
        p2->round.prev_round_points[local] = view->prev_round_points[s];
    }

    /* ---- 13. Rogue/Duel pending effect winners (remapped) ---- */
    p2->round.transmute_round.rogue_pending_winner =
        (view->rogue_pending_winner >= 0)
            ? remap_seat(view->rogue_pending_winner, my) : -1;
    p2->round.transmute_round.duel_pending_winner =
        (view->duel_pending_winner >= 0)
            ? remap_seat(view->duel_pending_winner, my) : -1;

    /* Rogue suit reveal */
    p2->round.transmute_round.rogue_chosen_suit = (int)view->rogue_chosen_suit;
    p2->round.transmute_round.rogue_chosen_target =
        (view->rogue_chosen_target >= 0)
            ? remap_seat(view->rogue_chosen_target, my) : -1;
    p2->round.transmute_round.rogue_revealed_count =
        (int)view->rogue_revealed_count;
    for (int i = 0; i < view->rogue_revealed_count && i < MAX_HAND_SIZE; i++)
        p2->round.transmute_round.rogue_revealed_cards[i] =
            net_card_to_game(view->rogue_revealed_cards[i]);

    /* Duel */
    p2->round.transmute_round.duel_chosen_card_idx =
        (int)view->duel_chosen_card_idx;
    p2->round.transmute_round.duel_chosen_target =
        (view->duel_chosen_target >= 0)
            ? remap_seat(view->duel_chosen_target, my) : -1;
    p2->round.transmute_round.duel_revealed_card =
        net_card_to_game(view->duel_revealed_card);

    /* ---- 14. Round-end transmutation effects (remapped) ---- */
    for (int i = 0; i < NUM_PLAYERS; i++) {
        int server_seat = (i + my) % NUM_PLAYERS;
        p2->round.transmute_round.martyr_flags[i] =
            view->martyr_flags[server_seat];
        p2->round.transmute_round.gatherer_reduction[i] =
            view->gatherer_reduction[server_seat];
    }
}
