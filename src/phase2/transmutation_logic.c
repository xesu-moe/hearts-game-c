/* ============================================================
 * @deps-implements: transmutation_logic.h
 * @deps-requires: transmutation_logic.h, transmutation.h, phase2_defs.h
 *                 (phase2_get_transmutation), phase2_state.h,
 *                 core/hand.h, core/trick.h, core/card.h, core/game_state.h,
 *                 string.h, raylib.h
 * @deps-last-changed: 2026-03-20 — Mirror: added transmute_resolve_effect(),
 *                     transmute_is_effective_fog(), transmute_effect_name(),
 *                     updated transmute_on_trick_complete() to use resolved_effects[]
 * ============================================================ */

#include "transmutation_logic.h"

#include <string.h>

#include <raylib.h>

#include "core/game_state.h"
#include "phase2_defs.h"
#include "phase2_state.h"

/* ----------------------------------------------------------------
 * Inventory
 * ---------------------------------------------------------------- */

void transmute_inv_init(TransmuteInventory *inv)
{
    for (int i = 0; i < MAX_TRANSMUTE_INVENTORY; i++) {
        inv->items[i] = -1;
    }
    inv->count = 0;
}

bool transmute_inv_add(TransmuteInventory *inv, int transmutation_id)
{
    if (inv->count >= MAX_TRANSMUTE_INVENTORY) return false;
    inv->items[inv->count] = transmutation_id;
    inv->count++;
    return true;
}

bool transmute_inv_remove(TransmuteInventory *inv, int slot_index)
{
    if (slot_index < 0 || slot_index >= inv->count) return false;
    inv->count--;
    for (int i = slot_index; i < inv->count; i++) {
        inv->items[i] = inv->items[i + 1];
    }
    inv->items[inv->count] = -1;
    return true;
}

/* ----------------------------------------------------------------
 * Hand transmute state
 * ---------------------------------------------------------------- */

void transmute_hand_init(HandTransmuteState *hts)
{
    for (int i = 0; i < MAX_HAND_SIZE; i++) {
        hts->slots[i].transmutation_id = -1;
        hts->slots[i].original_card = CARD_NONE;
        hts->slots[i].transmuter_player = -1;
    }
}

bool transmute_apply(Hand *hand, HandTransmuteState *hts,
                     TransmuteInventory *inv,
                     int hand_index, int transmutation_id,
                     int transmuter_player)
{
    if (hand_index < 0 || hand_index >= hand->count) return false;

    const TransmutationDef *td = phase2_get_transmutation(transmutation_id);
    if (!td) return false;

    /* Find and consume from inventory */
    int inv_slot = -1;
    for (int i = 0; i < inv->count; i++) {
        if (inv->items[i] == transmutation_id) {
            inv_slot = i;
            break;
        }
    }
    if (inv_slot < 0) return false;

    /* Save original card */
    hts->slots[hand_index].original_card = hand->cards[hand_index];
    hts->slots[hand_index].transmutation_id = transmutation_id;
    hts->slots[hand_index].transmuter_player = transmuter_player;

    /* Fog: keep original card identity (visual-only effect) */
    if (td->effect != TEFFECT_FOG_HIDDEN) {
        /* Replace hand card with transmuted version */
        hand->cards[hand_index].suit = td->result_suit;
        hand->cards[hand_index].rank = td->result_rank;
    }

    /* Consume from inventory */
    transmute_inv_remove(inv, inv_slot);

    return true;
}

void transmute_hand_remove_at(HandTransmuteState *hts, int index, int hand_count)
{
    if (index < 0 || hand_count < 0 || hand_count >= MAX_HAND_SIZE) return;
    /* Shift slots left to stay in sync with hand (hand_count is AFTER removal) */
    for (int i = index; i < hand_count; i++) {
        hts->slots[i] = hts->slots[i + 1];
    }
    /* Clear the now-unused last slot */
    hts->slots[hand_count].transmutation_id = -1;
    hts->slots[hand_count].original_card = CARD_NONE;
    hts->slots[hand_count].transmuter_player = -1;
}

void transmute_hand_sort_sync(HandTransmuteState *hts, const int *perm, int count)
{
    TransmuteSlot tmp[MAX_HAND_SIZE];
    for (int i = 0; i < count; i++) {
        tmp[i] = hts->slots[perm[i]];
    }
    for (int i = 0; i < count; i++) {
        hts->slots[i] = tmp[i];
    }
}

/* ----------------------------------------------------------------
 * Query
 * ---------------------------------------------------------------- */

bool transmute_is_transmuted(const HandTransmuteState *hts, int idx)
{
    if (idx < 0 || idx >= MAX_HAND_SIZE) return false;
    return hts->slots[idx].transmutation_id >= 0;
}

const TransmutationDef *transmute_get_def(const HandTransmuteState *hts, int idx)
{
    if (!transmute_is_transmuted(hts, idx)) return NULL;
    return phase2_get_transmutation(hts->slots[idx].transmutation_id);
}

Card transmute_get_original(const HandTransmuteState *hts, int idx)
{
    if (!transmute_is_transmuted(hts, idx)) return CARD_NONE;
    return hts->slots[idx].original_card;
}

bool transmute_is_fog(const HandTransmuteState *hts, int idx)
{
    const TransmutationDef *td = transmute_get_def(hts, idx);
    return td && td->effect == TEFFECT_FOG_HIDDEN;
}

int transmute_get_transmuter(const HandTransmuteState *hts, int idx)
{
    if (idx < 0 || idx >= MAX_HAND_SIZE) return -1;
    return hts->slots[idx].transmuter_player;
}

/* ----------------------------------------------------------------
 * Trick resolution helpers
 * ---------------------------------------------------------------- */

int transmute_card_points(const HandTransmuteState *hts, int idx, Card card)
{
    const TransmutationDef *td = transmute_get_def(hts, idx);
    if (td && td->custom_points >= 0) return td->custom_points;
    return card_points(card);
}

bool transmute_can_follow_suit(const HandTransmuteState *hts, int idx, Suit suit)
{
    const TransmutationDef *td = transmute_get_def(hts, idx);
    if (!td) return false; /* not transmuted, use normal rules */
    if (td->suit_mask == SUIT_MASK_NONE) return false; /* no mask override */
    return (td->suit_mask & (1 << suit)) != 0;
}

bool transmute_is_always_win(const HandTransmuteState *hts, int idx)
{
    const TransmutationDef *td = transmute_get_def(hts, idx);
    return td && td->special == TRANSMUTE_ALWAYS_WIN;
}

bool transmute_is_always_lose(const HandTransmuteState *hts, int idx)
{
    const TransmutationDef *td = transmute_get_def(hts, idx);
    return td && td->special == TRANSMUTE_ALWAYS_LOSE;
}

int transmute_trick_get_winner(const Trick *trick, const TrickTransmuteInfo *tti)
{
    if (!trick_is_complete(trick)) return -1;

    /* Check for ALWAYS_WIN / ALWAYS_LOSE cards */
    bool has_special = false;
    if (tti) {
        for (int i = 0; i < CARDS_PER_TRICK; i++) {
            if (tti->transmutation_ids[i] >= 0) {
                const TransmutationDef *td =
                    phase2_get_transmutation(tti->transmutation_ids[i]);
                if (td && td->special != TRANSMUTE_NORMAL) {
                    has_special = true;
                    break;
                }
            }
        }
    }

    if (!has_special) {
        return trick_get_winner(trick);
    }

    /* Find the ALWAYS_WIN card (last one wins if multiple) */
    int always_win_idx = -1;
    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        if (tti->transmutation_ids[i] >= 0) {
            const TransmutationDef *td =
                phase2_get_transmutation(tti->transmutation_ids[i]);
            if (td && td->special == TRANSMUTE_ALWAYS_WIN) {
                always_win_idx = i;
            }
        }
    }

    if (always_win_idx >= 0) {
        return trick->player_ids[always_win_idx];
    }

    /* No ALWAYS_WIN — fall back to normal, but exclude ALWAYS_LOSE cards */
    int best_idx = -1;
    Rank best_rank = RANK_2;
    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        /* Skip ALWAYS_LOSE cards */
        if (tti->transmutation_ids[i] >= 0) {
            const TransmutationDef *td =
                phase2_get_transmutation(tti->transmutation_ids[i]);
            if (td && td->special == TRANSMUTE_ALWAYS_LOSE) continue;
        }

        if (trick->cards[i].suit == trick->lead_suit) {
            if (best_idx < 0 || trick->cards[i].rank > best_rank) {
                best_idx = i;
                best_rank = trick->cards[i].rank;
            }
        }
    }

    if (best_idx >= 0) {
        return trick->player_ids[best_idx];
    }

    /* All lead-suit cards are ALWAYS_LOSE — fallback to standard */
    return trick_get_winner(trick);
}

int transmute_trick_count_points(const Trick *trick, const TrickTransmuteInfo *tti)
{
    if (!tti) return trick_count_points(trick);

    int points = 0;
    for (int i = 0; i < trick->num_played; i++) {
        if (tti->transmutation_ids[i] >= 0) {
            const TransmutationDef *td =
                phase2_get_transmutation(tti->transmutation_ids[i]);
            if (td && td->custom_points >= 0) {
                points += td->custom_points;
                continue;
            }
        }
        points += card_points(trick->cards[i]);
    }
    return points;
}

bool transmute_is_valid_play(const Trick *trick, const Hand *hand,
                             const HandTransmuteState *hts, int hand_index,
                             Card card, bool hearts_broken, bool first_trick)
{
    /* Check if this card is transmuted with a suit_mask */
    if (hts && transmute_is_transmuted(hts, hand_index)) {
        const TransmutationDef *td = transmute_get_def(hts, hand_index);
        if (td && td->suit_mask != SUIT_MASK_NONE) {
            /* Must have the card in hand */
            if (hand_index < 0 || hand_index >= hand->count) return false;
            if (!card_equals(hand->cards[hand_index], card)) return false;

            bool leading = (trick->num_played == 0);

            if (first_trick && leading) {
                /* Must play 2 of clubs on first trick lead */
                Card two_of_clubs = {SUIT_CLUBS, RANK_2};
                return card_equals(card, two_of_clubs);
            }

            /* First trick following: can't play penalty cards unless forced */
            if (first_trick && !leading) {
                if (hand_has_suit(hand, trick->lead_suit)) {
                    return (td->suit_mask & (1 << trick->lead_suit)) != 0;
                }
                /* Can't follow suit: check if this is a penalty card */
                bool is_penalty = (td->custom_points > 0) ||
                                  (td->custom_points < 0 &&
                                   card_points(card) > 0);
                if (is_penalty) {
                    /* Only allow if all cards in hand are penalty */
                    for (int i = 0; i < hand->count; i++) {
                        if (i == hand_index) continue;
                        if (card_points(hand->cards[i]) == 0) {
                            /* Check transmuted cards too */
                            if (transmute_is_transmuted(hts, i)) {
                                const TransmutationDef *otd =
                                    transmute_get_def(hts, i);
                                if (otd && otd->custom_points > 0)
                                    continue;
                            }
                            return false;
                        }
                    }
                }
                return true;
            }

            if (td->suit_mask == SUIT_MASK_ALL) {
                /* Suitless: can be played anytime */
                return true;
            }

            if (!leading) {
                /* Following: card can follow if its mask includes lead suit */
                if (td->suit_mask & (1 << trick->lead_suit)) {
                    return true;
                }
                /* If player has no cards of lead suit (considering masks), ok */
                bool can_follow = false;
                for (int i = 0; i < hand->count; i++) {
                    if (i == hand_index) continue;
                    if (hand->cards[i].suit == trick->lead_suit) {
                        can_follow = true;
                        break;
                    }
                    /* Check if another transmuted card can follow */
                    if (transmute_is_transmuted(hts, i)) {
                        const TransmutationDef *otd = transmute_get_def(hts, i);
                        if (otd && (otd->suit_mask & (1 << trick->lead_suit))) {
                            can_follow = true;
                            break;
                        }
                    }
                }
                return !can_follow;
            }

            /* Leading with a masked card */
            if (!hearts_broken) {
                /* Can't lead hearts-only mask unless all cards are hearts */
                bool only_hearts_mask = (td->suit_mask == SUIT_MASK_HEARTS);
                if (only_hearts_mask) {
                    /* If any other card in hand is non-hearts, can't lead this */
                    for (int i = 0; i < hand->count; i++) {
                        if (i == hand_index) continue;
                        if (hand->cards[i].suit != SUIT_HEARTS) {
                            /* Check if this other card is also hearts-only mask */
                            if (transmute_is_transmuted(hts, i)) {
                                const TransmutationDef *otd = transmute_get_def(hts, i);
                                if (otd && otd->suit_mask != SUIT_MASK_NONE &&
                                    !(otd->suit_mask & ~SUIT_MASK_HEARTS)) {
                                    continue; /* Also hearts-only */
                                }
                            }
                            return false; /* Has a non-hearts alternative */
                        }
                    }
                }
            }
            return true;
        }
    }

    /* No transmutation override — use standard rules */
    return trick_is_valid_play(trick, hand, card, hearts_broken, first_trick);
}

/* ----------------------------------------------------------------
 * Effect queries
 * ---------------------------------------------------------------- */

bool transmute_effect_affects_score(int transmutation_id)
{
    if (transmutation_id < 0) return false;
    const TransmutationDef *td = phase2_get_transmutation(transmutation_id);
    if (!td) return false;
    return td->effect == TEFFECT_WOTT_DUPLICATE_ROUND_POINTS ||
           td->effect == TEFFECT_WOTT_REDUCE_SCORE_3 ||
           td->effect == TEFFECT_WOTT_REDUCE_SCORE_1 ||
           td->effect == TEFFECT_MIRROR; /* conservative: may resolve to scoring effect */
}

TransmuteEffect transmute_resolve_effect(int transmutation_id, const Phase2State *p2)
{
    if (transmutation_id < 0) return TEFFECT_NONE;
    const TransmutationDef *td = phase2_get_transmutation(transmutation_id);
    if (!td) return TEFFECT_NONE;
    if (td->effect != TEFFECT_MIRROR) return td->effect;
    /* Mirror: return last globally-played resolved effect (implicit chain) */
    return p2 ? p2->last_played_resolved_effect : TEFFECT_NONE;
}

bool transmute_is_effective_fog(const HandTransmuteState *hts, int idx,
                                const Phase2State *p2)
{
    if (!transmute_is_transmuted(hts, idx)) return false;
    const TransmutationDef *td = transmute_get_def(hts, idx);
    if (!td) return false;
    if (td->effect == TEFFECT_FOG_HIDDEN) return true;
    if (td->effect == TEFFECT_MIRROR && p2) {
        return p2->last_played_resolved_effect == TEFFECT_FOG_HIDDEN;
    }
    return false;
}

const char *transmute_effect_name(TransmuteEffect eff)
{
    switch (eff) {
    case TEFFECT_NONE:                       return "None";
    case TEFFECT_WOTT_DUPLICATE_ROUND_POINTS: return "Martyr";
    case TEFFECT_WOTT_REDUCE_SCORE_3:        return "Gatherer";
    case TEFFECT_WOTT_REVEAL_OPPONENT_CARD:  return "Rogue";
    case TEFFECT_WOTT_REDUCE_SCORE_1:        return "Pendulum";
    case TEFFECT_WOTT_SWAP_CARD:             return "Duel";
    case TEFFECT_FOG_HIDDEN:                 return "Fog";
    case TEFFECT_MIRROR:                     return "Mirror";
    default:                                 return "Unknown";
    }
}

/* ----------------------------------------------------------------
 * Round-scoped effect tracking
 * ---------------------------------------------------------------- */

void transmute_round_state_init(TransmuteRoundState *trs)
{
    memset(trs, 0, sizeof(*trs));
    trs->rogue_pending_winner = -1;
    trs->duel_pending_winner = -1;
}

void transmute_on_trick_complete(Phase2State *p2, const Trick *trick,
                                  int winner, const TrickTransmuteInfo *tti)
{
    (void)trick; /* Reserved for future effects that inspect trick cards */
    if (!p2 || !tti || winner < 0 || winner >= NUM_PLAYERS) return;

    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        if (tti->transmutation_ids[i] < 0) continue;
        TransmuteEffect eff = tti->resolved_effects[i];

        if (eff == TEFFECT_WOTT_DUPLICATE_ROUND_POINTS) {
            p2->round.transmute_round.martyr_flags[winner] = true;
            TraceLog(LOG_INFO, "TRANSMUTE: Martyr effect flagged for player %d",
                     winner);
        } else if (eff == TEFFECT_WOTT_REDUCE_SCORE_3) {
            p2->round.transmute_round.gatherer_reduction[winner] += 3;
            TraceLog(LOG_INFO,
                     "TRANSMUTE: Gatherer +3 reduction for player %d (total %d)",
                     winner, p2->round.transmute_round.gatherer_reduction[winner]);
        } else if (eff == TEFFECT_WOTT_REVEAL_OPPONENT_CARD) {
            p2->round.transmute_round.rogue_pending_winner = winner;
            TraceLog(LOG_INFO,
                     "TRANSMUTE: Rogue reveal flagged for player %d", winner);
        } else if (eff == TEFFECT_WOTT_REDUCE_SCORE_1) {
            p2->round.transmute_round.gatherer_reduction[winner] += 1;
            TraceLog(LOG_INFO,
                     "TRANSMUTE: Pendulum +1 reduction for player %d (total %d)",
                     winner, p2->round.transmute_round.gatherer_reduction[winner]);
        } else if (eff == TEFFECT_WOTT_SWAP_CARD) {
            p2->round.transmute_round.duel_pending_winner = winner;
            TraceLog(LOG_INFO,
                     "TRANSMUTE: Duel swap flagged for player %d", winner);
        }
    }
}

void transmute_apply_round_end(Phase2State *p2,
                                int round_points[NUM_PLAYERS],
                                int total_scores[NUM_PLAYERS])
{
    if (!p2) return;

    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (p2->round.transmute_round.martyr_flags[i]) {
            int dup = round_points[i];
            round_points[i] += dup;
            total_scores[i] += dup;
            TraceLog(LOG_INFO,
                     "TRANSMUTE: Martyr doubles player %d round_points (%d -> %d), "
                     "new total = %d",
                     i, dup, round_points[i], total_scores[i]);
        }
    }

    /* Gatherer: reduce scores (floor at 0).
     * Applied after Martyr so the reduction offsets the final values,
     * including any Martyr penalty — this is intentional. */
    for (int i = 0; i < NUM_PLAYERS; i++) {
        int red = p2->round.transmute_round.gatherer_reduction[i];
        if (red > 0) {
            round_points[i] -= red;
            if (round_points[i] < 0) round_points[i] = 0;
            total_scores[i] -= red;
            if (total_scores[i] < 0) total_scores[i] = 0;
            TraceLog(LOG_INFO,
                     "TRANSMUTE: Gatherer reduces player %d by %d, "
                     "round = %d, total = %d",
                     i, red, round_points[i], total_scores[i]);
        }
    }
}

/* ----------------------------------------------------------------
 * Inter-player card swap (Duel effect)
 * ---------------------------------------------------------------- */

void transmute_swap_between_players(GameState *gs, Phase2State *p2,
                                     int pa, int idx_a, int pb, int idx_b)
{
    if (pa < 0 || pa >= NUM_PLAYERS || pb < 0 || pb >= NUM_PLAYERS) return;
    if (idx_a < 0 || idx_a >= gs->players[pa].hand.count) return;
    if (idx_b < 0 || idx_b >= gs->players[pb].hand.count) return;

    /* Swap card values */
    Card tmp = gs->players[pa].hand.cards[idx_a];
    gs->players[pa].hand.cards[idx_a] = gs->players[pb].hand.cards[idx_b];
    gs->players[pb].hand.cards[idx_b] = tmp;

    /* Swap transmutation metadata */
    TransmuteSlot tmp_slot = p2->players[pa].hand_transmutes.slots[idx_a];
    p2->players[pa].hand_transmutes.slots[idx_a] =
        p2->players[pb].hand_transmutes.slots[idx_b];
    p2->players[pb].hand_transmutes.slots[idx_b] = tmp_slot;
}

/* ----------------------------------------------------------------
 * AI
 * ---------------------------------------------------------------- */

void transmute_ai_apply(Hand *hand, HandTransmuteState *hts,
                        TransmuteInventory *inv, bool is_passing,
                        int player_id)
{
    if (inv->count <= 0) return;

    /* Simple AI: apply one transmutation to first eligible card.
     * During passing: prefer negative transmutations on high cards.
     * During play: prefer positive transmutations on low cards. */

    for (int s = 0; s < inv->count; s++) {
        const TransmutationDef *td = phase2_get_transmutation(inv->items[s]);
        if (!td) continue;

        /* Skip negative transmutations when not passing */
        if (!is_passing && td->negative) continue;
        /* Skip positive transmutations when passing (they'd be given away) */
        if (is_passing && !td->negative) continue;

        /* Find a card to transmute */
        int target = -1;
        if (is_passing && td->negative) {
            /* Pick highest-rank non-transmuted card */
            Rank best_rank = 0;
            bool found = false;
            for (int i = 0; i < hand->count; i++) {
                if (transmute_is_transmuted(hts, i)) continue;
                if (!found || hand->cards[i].rank > best_rank) {
                    best_rank = hand->cards[i].rank;
                    target = i;
                    found = true;
                }
            }
        } else {
            /* Pick lowest-rank non-transmuted card */
            Rank worst_rank = RANK_A;
            bool found = false;
            for (int i = 0; i < hand->count; i++) {
                if (transmute_is_transmuted(hts, i)) continue;
                if (!found || hand->cards[i].rank < worst_rank) {
                    worst_rank = hand->cards[i].rank;
                    target = i;
                    found = true;
                }
            }
        }

        if (target >= 0) {
            transmute_apply(hand, hts, inv, target, td->id, player_id);
            return; /* Apply only one per phase */
        }
    }
}
