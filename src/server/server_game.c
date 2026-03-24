/* ============================================================
 * @deps-implements: server_game.h
 * @deps-requires: server_game.h, core/game_state.h, core/hand.h, core/trick.h,
 *                 core/card.h, core/input_cmd.h, phase2/phase2_defs.h,
 *                 phase2/contract_logic.h, phase2/transmutation_logic.h,
 *                 phase2/phase2_state.h,
 *                 stdlib.h, stdio.h, string.h
 * @deps-last-changed: 2026-03-23 — Step 6: State machine refactor + apply_cmd
 * ============================================================ */

#include "server_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/hand.h"
#include "core/trick.h"
#include "core/card.h"
#include "phase2/phase2_defs.h"
#include "phase2/contract_logic.h"
#include "phase2/transmutation_logic.h"

/* ---- Forward declarations for internal helpers ---- */

static void sv_reset_tti(TrickTransmuteInfo *tti);
static void sv_do_pass_phase(ServerGame *sg);
static void sv_do_play_phase(ServerGame *sg);
static void sv_do_scoring(ServerGame *sg);
static void sv_ai_select_pass(GameState *gs, int player_id);
static bool sv_ai_play_card(ServerGame *sg, int player_id);
static bool sv_play_card_with_transmute(ServerGame *sg, int player_id, Card card);
static void sv_resolve_trick(ServerGame *sg);
static void sv_execute_rogue_ai(ServerGame *sg, int winner);
static void sv_execute_duel_ai(ServerGame *sg, int winner);
static int  sv_determine_dealer(const ServerGame *sg);
static void sv_log_play(ServerGame *sg, int player_id);
static void sv_check_post_trick_effects(ServerGame *sg, int winner);

/* ---- Logging helpers ---- */

static const char *sv_dir_name(PassDirection dir)
{
    static const char *names[] = {"Left", "Right", "Across", "None"};
    return (dir >= 0 && dir < PASS_COUNT) ? names[dir] : "???";
}

static const char *sv_player_name(int id)
{
    static const char *names[] = {"Player 0", "Player 1", "Player 2", "Player 3"};
    return (id >= 0 && id < NUM_PLAYERS) ? names[id] : "???";
}

/* ---- Public API ---- */

void server_game_init(ServerGame *sg)
{
    memset(sg, 0, sizeof(*sg));
    game_state_init(&sg->gs);

    phase2_defs_init();
    contract_state_init(&sg->p2);
    sg->p2.enabled = true;

    sg->dealer_player = -1;
    sg->game_active = false;
    sg->pass_substate = SV_PASS_IDLE;
    sg->play_substate = SV_PLAY_IDLE;
    sg->draft_round = 0;
    sg->draft_initialized = false;
    sg->duel_target_player = -1;
    sg->duel_target_hand_index = -1;
    sv_reset_tti(&sg->current_tti);
}

void server_game_start(ServerGame *sg)
{
    /* Mark all players as human=false for server (all AI-driven by default).
     * Room layer overrides is_human for connected players after this call. */
    for (int i = 0; i < NUM_PLAYERS; i++) {
        sg->gs.players[i].is_human = false;
    }

    game_state_start_game(&sg->gs);
    sg->game_active = true;
    sg->dealer_player = -1;
    sg->pass_substate = SV_PASS_IDLE;
    sg->play_substate = SV_PLAY_IDLE;
    sg->draft_round = 0;
    sg->draft_initialized = false;
    sg->duel_target_player = -1;
    sg->duel_target_hand_index = -1;
    memset(sg->prev_round_points, 0, sizeof(sg->prev_round_points));

    printf("=== Game Started ===\n");
}

void server_game_tick(ServerGame *sg)
{
    if (!sg->game_active) return;

    switch (sg->gs.phase) {
    case PHASE_DEALING:
        /* Instant transition — no deal animation on server */
        printf("\n=== Round %d ===\n", sg->gs.round_number);
        sg->gs.phase = PHASE_PASSING;
        contract_round_reset(&sg->p2);
        sg->pass_done = false;
        sg->contracts_done = false;

        /* Initialize pass sub-state machine */
        sg->dealer_player = sv_determine_dealer(sg);
        if (sg->dealer_player >= 0) {
            sg->pass_substate = SV_PASS_DEALER_DIR;
        } else {
            /* Round 1: no dealer, skip to contract draft */
            printf("No dealer (round 1) — %s, %d cards\n",
                   sv_dir_name(sg->gs.pass_direction),
                   sg->gs.pass_card_count);
            sg->pass_substate = SV_PASS_CONTRACT_DRAFT;
        }
        sg->draft_round = 0;
        sg->draft_initialized = false;
        break;

    case PHASE_PASSING:
        sv_do_pass_phase(sg);
        break;

    case PHASE_PLAYING:
        sv_do_play_phase(sg);
        break;

    case PHASE_SCORING:
        sv_do_scoring(sg);
        break;

    case PHASE_GAME_OVER:
        sg->game_active = false;
        break;

    default:
        break;
    }
}

bool server_game_is_over(const ServerGame *sg)
{
    return !sg->game_active;
}

/* ================================================================
 * Command Application — validates and applies human player commands
 * ================================================================ */

bool server_game_apply_cmd(ServerGame *sg, int seat, const InputCmd *cmd)
{
    if (!sg->game_active) return false;
    if (seat < 0 || seat >= NUM_PLAYERS) return false;

    GameState *gs = &sg->gs;
    Phase2State *p2 = &sg->p2;

    switch (cmd->type) {

    case INPUT_CMD_DEALER_DIR:
        if (sg->pass_substate != SV_PASS_DEALER_DIR ||
            seat != sg->dealer_player) {
            printf("REJECTED: DEALER_DIR from seat %d (expected dealer %d, state %d)\n",
                   seat, sg->dealer_player, sg->pass_substate);
            return false;
        }
        {
            int dir = cmd->dealer_dir.direction;
            if (dir < 0 || dir >= 3) { /* LEFT, RIGHT, ACROSS only */
                printf("REJECTED: invalid direction %d\n", dir);
                return false;
            }
            gs->pass_direction = (PassDirection)dir;
            printf("Dealer %s picks direction: %s\n",
                   sv_player_name(seat), sv_dir_name(gs->pass_direction));
            sg->pass_substate = SV_PASS_DEALER_AMT;
        }
        return true;

    case INPUT_CMD_DEALER_AMT:
        if (sg->pass_substate != SV_PASS_DEALER_AMT ||
            seat != sg->dealer_player) return false;
        {
            int amt = cmd->dealer_amt.amount;
            if (amt < 2 || amt > 4) {
                printf("REJECTED: invalid pass amount %d\n", amt);
                return false;
            }
            gs->pass_card_count = amt;
            printf("Dealer %s picks amount: %d cards\n",
                   sv_player_name(seat), amt);
            sg->pass_substate = SV_PASS_DEALER_CONFIRM;
        }
        return true;

    case INPUT_CMD_DEALER_CONFIRM:
        if (sg->pass_substate != SV_PASS_DEALER_CONFIRM ||
            seat != sg->dealer_player) return false;
        printf("Dealer %s confirms\n", sv_player_name(seat));
        sg->pass_substate = SV_PASS_CONTRACT_DRAFT;
        sg->draft_round = 0;
        sg->draft_initialized = false;
        return true;

    case INPUT_CMD_SELECT_CONTRACT:
        if (sg->pass_substate != SV_PASS_CONTRACT_DRAFT) return false;
        {
            DraftState *draft = &p2->round.draft;
            if (draft->players[seat].has_picked_this_round) {
                printf("REJECTED: seat %d already picked this draft round\n", seat);
                return false;
            }
            int pair_idx = cmd->contract.contract_id;
            if (pair_idx < 0 || pair_idx >= draft->players[seat].available_count) {
                printf("REJECTED: invalid pair index %d\n", pair_idx);
                return false;
            }
            draft_pick(draft, seat, pair_idx);
            printf("Seat %d drafted contract (pair %d)\n", seat, pair_idx);
        }
        return true;

    case INPUT_CMD_SELECT_CARD:
        if (sg->pass_substate != SV_PASS_CARD_SELECT) return false;
        if (gs->pass_ready[seat]) {
            printf("REJECTED: seat %d already submitted pass cards\n", seat);
            return false;
        }
        {
            Card card = cmd->card.card;
            int pc = gs->pass_card_count;

            /* Verify card is in player's hand */
            bool in_hand = false;
            for (int i = 0; i < gs->players[seat].hand.count; i++) {
                if (card_equals(gs->players[seat].hand.cards[i], card)) {
                    in_hand = true;
                    break;
                }
            }
            if (!in_hand) {
                printf("REJECTED: seat %d selected card not in hand\n", seat);
                return false;
            }

            /* Count currently selected cards and check for duplicate */
            int selected_count = 0;
            int found = -1;
            for (int i = 0; i < MAX_PASS_CARD_COUNT; i++) {
                if (i < pc && gs->pass_selections[seat][i].rank != 0) {
                    if (card_equals(gs->pass_selections[seat][i], card)) {
                        found = i;
                    }
                    selected_count++;
                }
            }

            if (found >= 0) {
                /* Deselect: shift remaining down */
                for (int i = found; i < selected_count - 1; i++) {
                    gs->pass_selections[seat][i] = gs->pass_selections[seat][i + 1];
                }
                memset(&gs->pass_selections[seat][selected_count - 1], 0, sizeof(Card));
                selected_count--;
            } else {
                if (selected_count >= pc) {
                    printf("REJECTED: seat %d already selected %d cards\n",
                           seat, pc);
                    return false;
                }
                gs->pass_selections[seat][selected_count] = card;
                selected_count++;
            }

            /* Auto-submit when the right number of cards are selected */
            if (selected_count == pc) {
                Card pass_cards[MAX_PASS_CARD_COUNT];
                for (int i = 0; i < pc; i++) {
                    pass_cards[i] = gs->pass_selections[seat][i];
                }
                game_state_select_pass(gs, seat, pass_cards, pc);
                printf("Seat %d submitted %d pass cards\n", seat, pc);
            }
        }
        return true;

    case INPUT_CMD_CONFIRM:
        /* CONFIRM during pass phase — if cards are selected but not yet
         * submitted (shouldn't happen with auto-submit, but handle it) */
        if (sg->pass_substate == SV_PASS_CARD_SELECT && gs->pass_ready[seat]) {
            return true; /* Already ready, confirm is a no-op */
        }
        return false;

    case INPUT_CMD_PLAY_CARD:
        if (sg->play_substate != SV_PLAY_WAIT_TURN) return false;
        if (game_state_current_player(gs) != seat) {
            printf("REJECTED: PLAY_CARD from seat %d, not their turn (current: %d)\n",
                   seat, game_state_current_player(gs));
            return false;
        }
        {
            Card card = cmd->card.card;

            /* Record hearts_broken state on first card of trick */
            if (gs->current_trick.num_played == 0) {
                sg->hearts_broken_at_trick_start = gs->hearts_broken;
            }

            if (!sv_play_card_with_transmute(sg, seat, card)) {
                printf("REJECTED: illegal play %s from seat %d\n",
                       card_name(card), seat);
                return false;
            }
            sv_log_play(sg, seat);

            /* If trick is now complete, advance to trick resolution */
            if (trick_is_complete(&gs->current_trick)) {
                sg->play_substate = SV_PLAY_TRICK_DONE;
            }
        }
        return true;

    case INPUT_CMD_ROGUE_REVEAL:
        if (sg->play_substate != SV_PLAY_ROGUE_WAIT) return false;
        if (seat != p2->round.transmute_round.rogue_pending_winner) return false;
        {
            int target = cmd->rogue_reveal.target_player;
            int hidx = cmd->rogue_reveal.hand_index;
            if (target < 0 || target >= NUM_PLAYERS || target == seat) return false;
            if (hidx < 0 || hidx >= gs->players[target].hand.count) return false;

            printf("  [Rogue] %s reveals %s's card: %s\n",
                   sv_player_name(seat), sv_player_name(target),
                   card_name(gs->players[target].hand.cards[hidx]));
            p2->round.transmute_round.rogue_pending_winner = -1;

            /* Check for duel after rogue */
            if (gs->phase == PHASE_PLAYING &&
                p2->round.transmute_round.duel_pending_winner >= 0) {
                int dw = p2->round.transmute_round.duel_pending_winner;
                if (gs->players[dw].is_human) {
                    sg->play_substate = SV_PLAY_DUEL_PICK_WAIT;
                } else {
                    sv_execute_duel_ai(sg, dw);
                    sg->play_substate = SV_PLAY_WAIT_TURN;
                }
            } else {
                sg->play_substate = SV_PLAY_WAIT_TURN;
            }
        }
        return true;

    case INPUT_CMD_DUEL_PICK:
        if (sg->play_substate != SV_PLAY_DUEL_PICK_WAIT) return false;
        if (seat != p2->round.transmute_round.duel_pending_winner) return false;
        {
            int target = cmd->duel_pick.target_player;
            int hidx = cmd->duel_pick.hand_index;
            if (target < 0 || target >= NUM_PLAYERS || target == seat) return false;
            if (hidx < 0 || hidx >= gs->players[target].hand.count) return false;

            /* Store target for DUEL_GIVE step */
            sg->duel_target_player = target;
            sg->duel_target_hand_index = hidx;
            sg->play_substate = SV_PLAY_DUEL_GIVE_WAIT;
        }
        return true;

    case INPUT_CMD_DUEL_GIVE:
        if (sg->play_substate != SV_PLAY_DUEL_GIVE_WAIT) return false;
        if (seat != p2->round.transmute_round.duel_pending_winner) return false;
        {
            int own_idx = cmd->duel_give.hand_index;
            if (own_idx < 0 || own_idx >= gs->players[seat].hand.count) return false;

            /* Validate stored target is still valid */
            int target = sg->duel_target_player;
            int target_idx = sg->duel_target_hand_index;
            if (target < 0 || target >= NUM_PLAYERS ||
                target_idx < 0 || target_idx >= gs->players[target].hand.count) {
                printf("REJECTED: duel target no longer valid\n");
                p2->round.transmute_round.duel_pending_winner = -1;
                sg->play_substate = SV_PLAY_WAIT_TURN;
                return false;
            }

            transmute_swap_between_players(gs, p2, seat, own_idx,
                                           target, target_idx);
            printf("  [Duel] %s swaps a card with %s\n",
                   sv_player_name(seat), sv_player_name(target));

            p2->round.transmute_round.duel_pending_winner = -1;
            sg->duel_target_player = -1;
            sg->duel_target_hand_index = -1;
            sg->play_substate = SV_PLAY_WAIT_TURN;
        }
        return true;

    default:
        printf("REJECTED: unhandled command type %d from seat %d\n",
               cmd->type, seat);
        return false;
    }
}

/* ================================================================
 * Pass Phase — Sub-state machine
 * ================================================================ */

static void sv_do_pass_phase(ServerGame *sg)
{
    GameState *gs = &sg->gs;
    Phase2State *p2 = &sg->p2;

    switch (sg->pass_substate) {

    case SV_PASS_DEALER_DIR:
        if (!gs->players[sg->dealer_player].is_human) {
            /* AI dealer picks randomly */
            gs->pass_direction = (PassDirection)(rand() % 3);
            printf("Dealer: %s — %s\n",
                   sv_player_name(sg->dealer_player),
                   sv_dir_name(gs->pass_direction));
            sg->pass_substate = SV_PASS_DEALER_AMT;
        }
        /* Human: wait for DEALER_DIR command */
        break;

    case SV_PASS_DEALER_AMT:
        if (!gs->players[sg->dealer_player].is_human) {
            static const int amounts[] = {2, 3, 4};
            gs->pass_card_count = amounts[rand() % 3];
            printf("Dealer: %s — %d cards\n",
                   sv_player_name(sg->dealer_player),
                   gs->pass_card_count);
            sg->pass_substate = SV_PASS_DEALER_CONFIRM;
        }
        break;

    case SV_PASS_DEALER_CONFIRM:
        if (!gs->players[sg->dealer_player].is_human) {
            sg->pass_substate = SV_PASS_CONTRACT_DRAFT;
            sg->draft_round = 0;
            sg->draft_initialized = false;
        }
        break;

    case SV_PASS_CONTRACT_DRAFT: {
        DraftState *draft = &p2->round.draft;

        /* Initialize draft on first entry */
        if (!sg->draft_initialized) {
            draft_generate_pool(draft);
            sg->draft_initialized = true;
            sg->draft_round = 0;
        }

        /* AI players pick automatically */
        for (int p = 0; p < NUM_PLAYERS; p++) {
            if (!gs->players[p].is_human && !draft->players[p].has_picked_this_round) {
                draft_ai_pick(draft, p);
            }
        }

        /* Check if all players have picked this round */
        if (draft_all_picked(draft)) {
            sg->draft_round++;
            if (sg->draft_round < DRAFT_ROUNDS) {
                draft_advance_round(draft);
            } else {
                draft_finalize(draft, p2);
                printf("Contracts drafted for all players\n");
                sg->contracts_done = true;
                sg->pass_substate = SV_PASS_CARD_SELECT;
            }
        }
        /* If humans haven't picked yet, wait */
        break;
    }

    case SV_PASS_CARD_SELECT:
        /* AI players select pass cards */
        for (int p = 0; p < NUM_PLAYERS; p++) {
            if (!gs->players[p].is_human && !gs->pass_ready[p]) {
                sv_ai_select_pass(gs, p);
            }
        }

        /* Check if all players are ready */
        {
            bool all_ready = true;
            for (int p = 0; p < NUM_PLAYERS; p++) {
                if (!gs->pass_ready[p]) { all_ready = false; break; }
            }
            if (all_ready) {
                sg->pass_substate = SV_PASS_EXECUTE;
            }
        }
        break;

    case SV_PASS_EXECUTE: {
        /* Record received cards for contract tracking */
        static const int pass_offsets[PASS_COUNT] = {1, 3, 2, 0};
        int offset = pass_offsets[gs->pass_direction];
        for (int p = 0; p < NUM_PLAYERS; p++) {
            int dest = (p + offset) % NUM_PLAYERS;
            contract_record_received_cards(p2, dest,
                                           gs->pass_selections[p],
                                           gs->pass_card_count);
        }
        game_state_execute_pass(gs);
        printf("Pass complete\n");
        sg->pass_substate = SV_PASS_TRANSMUTE;
        break;
    }

    case SV_PASS_TRANSMUTE:
        /* Apply transmutations AFTER pass, to the final hand */
        for (int p = 0; p < NUM_PLAYERS; p++) {
            transmute_ai_apply(&gs->players[p].hand,
                               &p2->players[p].hand_transmutes,
                               &p2->players[p].transmute_inv,
                               false, p);
        }
        printf("Transmutations applied\n");
        sg->pass_done = true;

        /* Transition to playing */
        gs->phase = PHASE_PLAYING;
        sg->pass_substate = SV_PASS_IDLE;
        sg->play_substate = SV_PLAY_WAIT_TURN;
        sg->hearts_broken_at_trick_start = gs->hearts_broken;
        sv_reset_tti(&sg->current_tti);
        break;

    default:
        break;
    }
}

/* ================================================================
 * Play Phase — Sub-state machine
 * ================================================================ */

static void sv_do_play_phase(ServerGame *sg)
{
    GameState *gs = &sg->gs;

    if (gs->phase != PHASE_PLAYING) return;

    switch (sg->play_substate) {

    case SV_PLAY_WAIT_TURN: {
        /* Check if trick is complete first */
        if (trick_is_complete(&gs->current_trick)) {
            sg->play_substate = SV_PLAY_TRICK_DONE;
            break;
        }

        int current = game_state_current_player(gs);
        if (current < 0) break;

        /* Record hearts_broken state before this trick for contract tracking */
        if (gs->current_trick.num_played == 0) {
            sg->hearts_broken_at_trick_start = gs->hearts_broken;
        }

        /* If current player is human, wait for PLAY_CARD command */
        if (gs->players[current].is_human) break;

        /* AI plays */
        if (!sv_ai_play_card(sg, current)) {
            fprintf(stderr, "ERROR: %s (%d cards) stuck at trick %d slot %d\n",
                    sv_player_name(current), gs->players[current].hand.count,
                    gs->tricks_played + 1, gs->current_trick.num_played);
            Hand *h = &gs->players[current].hand;
            if (h->count > 0) {
                Card card = h->cards[0];
                hand_remove_card(h, card);
                trick_play_card(&gs->current_trick, current, card);
                if (card.suit == SUIT_HEARTS) gs->hearts_broken = true;
            } else {
                sg->game_active = false;
                return;
            }
        }

        sv_log_play(sg, current);

        /* If trick now complete, transition to trick resolution */
        if (trick_is_complete(&gs->current_trick)) {
            sg->play_substate = SV_PLAY_TRICK_DONE;
        }
        break;
    }

    case SV_PLAY_TRICK_DONE:
        sv_resolve_trick(sg);
        break;

    case SV_PLAY_ROGUE_WAIT:
        /* Waiting for human ROGUE_REVEAL command — nothing to do */
        break;

    case SV_PLAY_DUEL_PICK_WAIT:
        /* Waiting for human DUEL_PICK command — nothing to do */
        break;

    case SV_PLAY_DUEL_GIVE_WAIT:
        /* Waiting for human DUEL_GIVE command — nothing to do */
        break;

    default:
        break;
    }
}

/* ================================================================
 * Play logging helper
 * ================================================================ */

static void sv_log_play(ServerGame *sg, int player_id)
{
    GameState *gs = &sg->gs;
    int slot = gs->current_trick.num_played - 1;
    if (slot < 0 || slot >= CARDS_PER_TRICK) return;

    Card played = gs->current_trick.cards[slot];
    if (gs->current_trick.num_played == 1) {
        printf("--- Trick %d ---\n", gs->tricks_played + 1);
    }
    const char *transmute_tag = "";
    char tag_buf[64];
    if (sg->current_tti.transmutation_ids[slot] >= 0) {
        const TransmutationDef *td = phase2_get_transmutation(
            sg->current_tti.transmutation_ids[slot]);
        if (td) {
            snprintf(tag_buf, sizeof(tag_buf), " [%s]", td->name);
            transmute_tag = tag_buf;
        }
    }
    printf("  %s %s %s%s\n",
           sv_player_name(player_id),
           gs->current_trick.num_played == 1 ? "leads" : "plays",
           card_name(played), transmute_tag);
}

/* ================================================================
 * Trick Resolution — Phase 2 aware (Bounty, Parasite, Shield, etc.)
 * Rogue/Duel effects are handled by the sub-state machine, not here.
 * ================================================================ */

static void sv_resolve_trick(ServerGame *sg)
{
    GameState *gs = &sg->gs;
    Phase2State *p2 = &sg->p2;
    TrickTransmuteInfo *tti = &sg->current_tti;

    int winner = -1;

    if (p2->enabled) {
        Trick saved_trick = gs->current_trick;
        TrickTransmuteInfo saved_tti = *tti;
        winner = transmute_trick_get_winner(&saved_trick, &saved_tti, p2);
        int points = transmute_trick_count_points(&saved_trick, &saved_tti);

        /* Bounty: redirect Q-spades points to the player who played Q-spades */
        bool has_bounty = false;
        for (int bi = 0; bi < CARDS_PER_TRICK; bi++) {
            if (saved_tti.resolved_effects[bi] == TEFFECT_BOUNTY_REDIRECT_QOS) {
                has_bounty = true;
                break;
            }
        }
        if (has_bounty) {
            bool has_trap = false;
            bool has_inversion = false;
            for (int ti = 0; ti < CARDS_PER_TRICK; ti++) {
                if (saved_tti.resolved_effects[ti] == TEFFECT_TRAP_DOUBLE_WITH_QOS)
                    has_trap = true;
                if (saved_tti.resolved_effects[ti] == TEFFECT_INVERSION_NEGATE_POINTS)
                    has_inversion = true;
            }
            for (int qi = 0; qi < CARDS_PER_TRICK; qi++) {
                if (saved_trick.cards[qi].suit == SUIT_SPADES &&
                    saved_trick.cards[qi].rank == RANK_Q) {
                    int qos_pts;
                    if (saved_tti.transmutation_ids[qi] >= 0) {
                        const TransmutationDef *td = phase2_get_transmutation(
                            saved_tti.transmutation_ids[qi]);
                        qos_pts = (td && td->custom_points >= 0)
                                      ? td->custom_points
                                      : card_points(saved_trick.cards[qi]);
                    } else {
                        qos_pts = card_points(saved_trick.cards[qi]);
                    }
                    if (has_trap) qos_pts *= 2;
                    if (has_inversion) qos_pts = -qos_pts;

                    int qp = saved_trick.player_ids[qi];
                    if (qp >= 0 && qp < NUM_PLAYERS) {
                        if (p2->shield_tricks_remaining[qp] > 0) {
                            printf("  [Shield] absorbed Bounty QoS for %s\n",
                                   sv_player_name(qp));
                            qos_pts = 0;
                        }
                        gs->players[qp].round_points += qos_pts;
                        points -= qos_pts;
                        if (qos_pts != 0) {
                            printf("  [Bounty] redirected %d QoS pts to %s\n",
                                   qos_pts, sv_player_name(qp));
                        }
                    }
                }
            }
        }

        /* Parasite: redirect points to card player(s) */
        bool has_parasite = false;
        for (int pi = 0; pi < CARDS_PER_TRICK; pi++) {
            if (saved_tti.resolved_effects[pi] == TEFFECT_PARASITE_REDIRECT_POINTS) {
                has_parasite = true;
                int pp = saved_trick.player_ids[pi];
                if (pp < 0 || pp >= NUM_PLAYERS) continue;
                int pp_points = points;
                if (p2->shield_tricks_remaining[pp] > 0) {
                    pp_points = 0;
                    printf("  [Shield] absorbed Parasite redirect for %s\n",
                           sv_player_name(pp));
                }
                gs->players[pp].round_points += pp_points;
                if (pp_points != 0) {
                    printf("  [Parasite] redirected %d pts to %s\n",
                           pp_points, sv_player_name(pp));
                }
            }
        }

        if (has_parasite) {
            for (int si = 0; si < NUM_PLAYERS; si++) {
                if (p2->shield_tricks_remaining[si] > 0)
                    p2->shield_tricks_remaining[si]--;
            }
            game_state_complete_trick_with(gs, winner, 0);
        } else {
            if (winner >= 0 && p2->shield_tricks_remaining[winner] > 0) {
                printf("  [Shield] absorbed trick for %s\n",
                       sv_player_name(winner));
                points = 0;
            }
            for (int si = 0; si < NUM_PLAYERS; si++) {
                if (p2->shield_tricks_remaining[si] > 0)
                    p2->shield_tricks_remaining[si]--;
            }
            game_state_complete_trick_with(gs, winner, points);
        }

        if (winner >= 0) {
            printf("  -> %s takes trick (%d pts)\n",
                   sv_player_name(winner), points);

            contract_on_trick_complete(p2, &saved_trick, winner,
                                       gs->tricks_played - 1,
                                       &saved_tti,
                                       sg->hearts_broken_at_trick_start);
            transmute_on_trick_complete(p2, &saved_trick, winner, &saved_tti);

            /* Consume binding flags */
            for (int bi = 0; bi < NUM_PLAYERS; bi++) {
                if (p2->binding_auto_win[bi]) {
                    p2->binding_auto_win[bi] = 0;
                }
            }
        }
    } else {
        /* Vanilla Hearts — no Phase 2 */
        winner = trick_get_winner(&gs->current_trick);
        int points = trick_count_points(&gs->current_trick);
        game_state_complete_trick(gs);
        if (winner >= 0) {
            printf("  -> %s takes trick (%d pts)\n",
                   sv_player_name(winner), points);
        }
    }

    /* Reset TTI for next trick */
    sv_reset_tti(&sg->current_tti);

    /* Handle post-trick effects (rogue, duel) or advance */
    sv_check_post_trick_effects(sg, winner);
}

/* Check for rogue/duel effects after trick resolution.
 * Sets play_substate appropriately. */
static void sv_check_post_trick_effects(ServerGame *sg, int winner)
{
    (void)winner; /* Winner used for future effect targeting */
    GameState *gs = &sg->gs;
    Phase2State *p2 = &sg->p2;

    /* If round/game is over, stop */
    if (gs->phase != PHASE_PLAYING) {
        sg->play_substate = SV_PLAY_IDLE;
        return;
    }

    /* Check rogue */
    if (p2->enabled &&
        p2->round.transmute_round.rogue_pending_winner >= 0) {
        int rw = p2->round.transmute_round.rogue_pending_winner;
        if (gs->players[rw].is_human) {
            sg->play_substate = SV_PLAY_ROGUE_WAIT;
            return;
        }
        /* AI rogue */
        sv_execute_rogue_ai(sg, rw);
        p2->round.transmute_round.rogue_pending_winner = -1;
    }

    /* Check duel */
    if (p2->enabled &&
        p2->round.transmute_round.duel_pending_winner >= 0) {
        int dw = p2->round.transmute_round.duel_pending_winner;
        if (gs->players[dw].is_human) {
            sg->play_substate = SV_PLAY_DUEL_PICK_WAIT;
            return;
        }
        /* AI duel */
        sv_execute_duel_ai(sg, dw);
        p2->round.transmute_round.duel_pending_winner = -1;
    }

    /* No pending effects — continue playing */
    sg->play_substate = SV_PLAY_WAIT_TURN;
}

/* ================================================================
 * Rogue/Duel AI Helpers (extracted from old sv_resolve_trick)
 * ================================================================ */

static void sv_execute_rogue_ai(ServerGame *sg, int winner)
{
    GameState *gs = &sg->gs;

    int out_p = -1, out_idx = -1;
    int best_count = 0;
    int candidates[NUM_PLAYERS];
    int num_candidates = 0;

    for (int p = 0; p < NUM_PLAYERS; p++) {
        if (p == winner) continue;
        int cnt = gs->players[p].hand.count;
        if (cnt <= 0) continue;
        if (cnt > best_count) {
            best_count = cnt;
            num_candidates = 0;
        }
        if (cnt == best_count) {
            candidates[num_candidates++] = p;
        }
    }
    if (num_candidates > 0) {
        out_p = candidates[rand() % num_candidates];
        out_idx = rand() % gs->players[out_p].hand.count;
        printf("  [Rogue] %s reveals %s's card: %s\n",
               sv_player_name(winner), sv_player_name(out_p),
               card_name(gs->players[out_p].hand.cards[out_idx]));
    }
    (void)out_idx; /* Reveal is log-only for now */
}

static void sv_execute_duel_ai(ServerGame *sg, int winner)
{
    GameState *gs = &sg->gs;
    Phase2State *p2 = &sg->p2;

    int out_p = -1, out_idx = -1;
    int best_count = 0;
    int candidates[NUM_PLAYERS];
    int num_candidates = 0;

    for (int p = 0; p < NUM_PLAYERS; p++) {
        if (p == winner) continue;
        int cnt = gs->players[p].hand.count;
        if (cnt <= 0) continue;
        if (cnt > best_count) {
            best_count = cnt;
            num_candidates = 0;
        }
        if (cnt == best_count) {
            candidates[num_candidates++] = p;
        }
    }
    if (num_candidates > 0) {
        out_p = candidates[rand() % num_candidates];
        out_idx = rand() % gs->players[out_p].hand.count;
        int own_count = gs->players[winner].hand.count;
        if (own_count > 0) {
            int own_idx = rand() % own_count;
            transmute_swap_between_players(gs, p2, winner, own_idx,
                                           out_p, out_idx);
            printf("  [Duel] %s swaps a card with %s\n",
                   sv_player_name(winner), sv_player_name(out_p));
        }
    }
}

/* ================================================================
 * Scoring — Evaluate contracts, apply round-end effects, advance
 * ================================================================ */

static void sv_do_scoring(ServerGame *sg)
{
    GameState *gs = &sg->gs;
    Phase2State *p2 = &sg->p2;

    /* Save round points before they get zeroed */
    for (int i = 0; i < NUM_PLAYERS; i++) {
        sg->prev_round_points[i] = gs->players[i].round_points;
    }

    /* Contract evaluation and rewards */
    if (p2->enabled) {
        for (int p = 0; p < NUM_PLAYERS; p++) {
            contract_evaluate_all(p2, p);
            contract_apply_rewards_all(p2, p);
        }

        /* Round-end transmutation effects */
        int rp[NUM_PLAYERS], ts[NUM_PLAYERS];
        for (int i = 0; i < NUM_PLAYERS; i++) {
            rp[i] = gs->players[i].round_points;
            ts[i] = gs->players[i].total_score;
        }
        transmute_apply_round_end(p2, rp, ts);
        for (int i = 0; i < NUM_PLAYERS; i++) {
            gs->players[i].round_points = rp[i];
            gs->players[i].total_score = ts[i];
        }
    }

    /* Print round scores */
    printf("Round %d scores: [", gs->round_number);
    for (int i = 0; i < NUM_PLAYERS; i++) {
        printf("%d%s", gs->players[i].round_points,
               i < NUM_PLAYERS - 1 ? ", " : "");
    }
    printf("]\n");

    /* Advance scoring */
    game_state_advance_scoring(gs);

    /* Print total scores */
    printf("Total scores:   [");
    for (int i = 0; i < NUM_PLAYERS; i++) {
        printf("%d%s", gs->players[i].total_score,
               i < NUM_PLAYERS - 1 ? ", " : "");
    }
    printf("]\n");

    if (gs->phase == PHASE_GAME_OVER) {
        int winners[NUM_PLAYERS];
        int wcount = game_state_get_winners(gs, winners);
        printf("\n=== GAME OVER ===\n");
        for (int i = 0; i < wcount; i++) {
            printf("Winner: %s (score: %d)\n",
                   sv_player_name(winners[i]),
                   gs->players[winners[i]].total_score);
        }
        sg->game_active = false;
    }

    /* Reset sub-states for next round */
    sg->pass_substate = SV_PASS_IDLE;
    sg->play_substate = SV_PLAY_IDLE;
}

/* ================================================================
 * AI Helpers
 * ================================================================ */

/* AI pass: select highest-point cards from hand */
static void sv_ai_select_pass(GameState *gs, int player_id)
{
    Hand *hand = &gs->players[player_id].hand;
    int pc = gs->pass_card_count;
    Card pass_cards[MAX_PASS_CARD_COUNT];
    int pass_count = 0;
    bool used[MAX_HAND_SIZE] = {false};

    for (int p = 0; p < pc; p++) {
        int best = -1;
        int best_points = -1;
        int best_rank = -1;
        for (int i = 0; i < hand->count; i++) {
            if (used[i]) continue;
            int pts = card_points(hand->cards[i]);
            int rnk = hand->cards[i].rank;
            if (pts > best_points || (pts == best_points && rnk > best_rank)) {
                best = i;
                best_points = pts;
                best_rank = rnk;
            }
        }
        if (best >= 0) {
            pass_cards[pass_count++] = hand->cards[best];
            used[best] = true;
        }
    }

    if (pass_count == pc) {
        game_state_select_pass(gs, player_id, pass_cards, pc);
    }
}

/* AI play: find first valid card, play with transmutation awareness */
static bool sv_ai_play_card(ServerGame *sg, int player_id)
{
    GameState *gs = &sg->gs;
    Phase2State *p2 = &sg->p2;
    const Hand *hand = &gs->players[player_id].hand;
    bool first_trick = (gs->tricks_played == 0);
    bool leading = (gs->current_trick.num_played == 0);
    bool cursed = p2->enabled && p2->curse_force_hearts[player_id];
    bool anchored = p2->enabled && p2->anchor_force_suit[player_id] >= 0;

    for (int i = 0; i < hand->count; i++) {
        bool valid;
        if (p2->enabled) {
            bool hb = gs->hearts_broken || (leading && cursed);
            valid = transmute_is_valid_play(
                &gs->current_trick, hand,
                &p2->players[player_id].hand_transmutes, i,
                hand->cards[i], hb, first_trick);
            if (leading && cursed && valid) {
                valid = transmute_curse_is_valid_lead(hand, hand->cards[i]);
            }
            if (leading && !cursed && anchored && valid) {
                valid = transmute_anchor_is_valid_lead(
                    hand, hand->cards[i],
                    p2->anchor_force_suit[player_id]);
            }
        } else {
            valid = game_state_is_valid_play(gs, player_id, hand->cards[i]);
        }
        if (valid) {
            return sv_play_card_with_transmute(sg, player_id, hand->cards[i]);
        }
    }

    /* Fallback: transmutation validation blocked all cards */
    if (first_trick && leading && p2->enabled) {
        Card two_clubs = {SUIT_CLUBS, RANK_2};
        HandTransmuteState *hts = &p2->players[player_id].hand_transmutes;
        for (int i = 0; i < hand->count; i++) {
            bool is_two_clubs = card_equals(hand->cards[i], two_clubs);
            if (!is_two_clubs && transmute_is_transmuted(hts, i)) {
                Card orig = transmute_get_original(hts, i);
                is_two_clubs = card_equals(orig, two_clubs);
            }
            if (is_two_clubs) {
                Card card = hand->cards[i];
                int tid = transmute_is_transmuted(hts, i)
                              ? hts->slots[i].transmutation_id : -1;
                int trick_slot = gs->current_trick.num_played;

                hand_remove_card(&gs->players[player_id].hand, card);
                trick_play_card(&gs->current_trick, player_id, card);

                if (trick_slot < CARDS_PER_TRICK) {
                    sg->current_tti.transmutation_ids[trick_slot] = tid;
                    sg->current_tti.transmuter_player[trick_slot] =
                        hts->slots[i].transmuter_player;
                    sg->current_tti.fogged[trick_slot] = hts->slots[i].fogged;
                    sg->current_tti.fog_transmuter[trick_slot] =
                        hts->slots[i].fogged ? hts->slots[i].fog_transmuter : -1;
                    TransmuteEffect resolved = transmute_resolve_effect(tid, p2);
                    sg->current_tti.resolved_effects[trick_slot] = resolved;
                    if (tid >= 0) {
                        p2->last_played_transmute_id = tid;
                        p2->last_played_resolved_effect = resolved;
                    }
                }
                transmute_hand_remove_at(hts, i, gs->players[player_id].hand.count);
                return true;
            }
        }
    }

    /* General fallback: try vanilla validation */
    for (int i = 0; i < hand->count; i++) {
        if (game_state_is_valid_play(gs, player_id, hand->cards[i])) {
            fprintf(stderr, "WARN: %s fallback to vanilla play: %s\n",
                    sv_player_name(player_id), card_name(hand->cards[i]));
            return game_state_play_card(gs, player_id, hand->cards[i]);
        }
    }

    /* Last resort: force play first card */
    fprintf(stderr, "WARN: %s force-play (trick %d, %d played, lead=%d, "
            "first=%d, hand=%d cards, hb=%d)\n",
            sv_player_name(player_id), gs->tricks_played,
            gs->current_trick.num_played, gs->lead_player,
            first_trick, hand->count, gs->hearts_broken);
    if (hand->count > 0) {
        Card card = hand->cards[0];
        hand_remove_card(&gs->players[player_id].hand, card);
        trick_play_card(&gs->current_trick, player_id, card);
        if (card.suit == SUIT_HEARTS) gs->hearts_broken = true;
        return true;
    }
    return false;
}

/* Play a card with transmutation tracking (no RenderState) */
static bool sv_play_card_with_transmute(ServerGame *sg, int player_id, Card card)
{
    GameState *gs = &sg->gs;
    Phase2State *p2 = &sg->p2;
    TrickTransmuteInfo *tti = &sg->current_tti;

    if (!p2->enabled) {
        return game_state_play_card(gs, player_id, card);
    }

    Hand *hand = &gs->players[player_id].hand;
    HandTransmuteState *hts = &p2->players[player_id].hand_transmutes;

    int hand_idx = -1;
    for (int i = 0; i < hand->count; i++) {
        if (card_equals(hand->cards[i], card)) {
            hand_idx = i;
            break;
        }
    }

    int trick_slot = gs->current_trick.num_played;
    int tid = -1;
    bool is_transmuted = (hand_idx >= 0 && transmute_is_transmuted(hts, hand_idx));
    if (is_transmuted) {
        tid = hts->slots[hand_idx].transmutation_id;
    }

    /* Curse: enforce hearts-only lead */
    bool leading = (gs->current_trick.num_played == 0);
    if (leading && p2->curse_force_hearts[player_id]) {
        if (!transmute_curse_is_valid_lead(hand, card)) return false;
    }

    /* Anchor: enforce suit lead */
    if (leading && !p2->curse_force_hearts[player_id] &&
        p2->anchor_force_suit[player_id] >= 0) {
        if (!transmute_anchor_is_valid_lead(
                hand, card, p2->anchor_force_suit[player_id]))
            return false;
    }

    bool ok;
    if (is_transmuted) {
        if (gs->phase != PHASE_PLAYING) return false;
        if (game_state_current_player(gs) != player_id) return false;
        bool first_trick = (gs->tricks_played == 0);
        bool hb = gs->hearts_broken || (leading && p2->curse_force_hearts[player_id]);
        if (!transmute_is_valid_play(&gs->current_trick, hand, hts,
                                     hand_idx, card, hb, first_trick)) {
            return false;
        }
        if (!hand_remove_card(hand, card)) return false;
        trick_play_card(&gs->current_trick, player_id, card);
        if (card.suit == SUIT_HEARTS) {
            gs->hearts_broken = true;
        }
        ok = true;
    } else {
        bool was_broken = gs->hearts_broken;
        if (leading && p2->curse_force_hearts[player_id]) {
            gs->hearts_broken = true;
        }
        ok = game_state_play_card(gs, player_id, card);
        if (!ok) {
            gs->hearts_broken = was_broken;
            return false;
        }
    }

    /* Consume curse/anchor after leading */
    if (leading && p2->curse_force_hearts[player_id]) {
        transmute_curse_consume(p2, gs, player_id);
    }
    if (leading && p2->anchor_force_suit[player_id] >= 0) {
        transmute_anchor_consume(p2, player_id);
    }

    /* Record in trick transmute info */
    if (trick_slot < CARDS_PER_TRICK) {
        tti->transmutation_ids[trick_slot] = tid;
        tti->transmuter_player[trick_slot] =
            (hand_idx >= 0) ? hts->slots[hand_idx].transmuter_player : -1;

        bool card_fogged = (hand_idx >= 0) && hts->slots[hand_idx].fogged;
        tti->fogged[trick_slot] = card_fogged;
        tti->fog_transmuter[trick_slot] =
            card_fogged ? hts->slots[hand_idx].fog_transmuter : -1;

        TransmuteEffect resolved = transmute_resolve_effect(tid, p2);
        tti->resolved_effects[trick_slot] = resolved;

        if (tid >= 0) {
            p2->last_played_transmute_id = tid;
            p2->last_played_resolved_effect = resolved;
        }
    }

    /* Sync hand transmute state */
    if (hand_idx >= 0) {
        transmute_hand_remove_at(hts, hand_idx, hand->count);
    }

    return true;
}

/* Determine dealer: highest previous round points, tiebreak total score, then lowest id */
static int sv_determine_dealer(const ServerGame *sg)
{
    if (sg->gs.round_number <= 1) return -1;

    int best = -1;
    int best_rp = -1;
    int best_ts = -1;

    for (int i = 0; i < NUM_PLAYERS; i++) {
        int rp = sg->prev_round_points[i];
        int ts = sg->gs.players[i].total_score;
        if (rp > best_rp || (rp == best_rp && ts > best_ts) ||
            (rp == best_rp && ts == best_ts && (best < 0))) {
            best = i;
            best_rp = rp;
            best_ts = ts;
        }
    }
    return best;
}

static void sv_reset_tti(TrickTransmuteInfo *tti)
{
    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        tti->transmutation_ids[i] = -1;
        tti->transmuter_player[i] = -1;
        tti->resolved_effects[i] = TEFFECT_NONE;
        tti->fogged[i] = false;
        tti->fog_transmuter[i] = -1;
    }
}
