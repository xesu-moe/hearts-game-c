/* ============================================================
 * @deps-implements: server_game.h
 * @deps-requires: server_game.h, net/protocol.h (NetPlayerView.rogue/duel_revealed_card, net_build_player_view),
 *                 core/game_state.h, core/hand.h, core/trick.h, core/card.h, core/input_cmd.h,
 *                 phase2/phase2_state.h, phase2/transmutation.h (TransmuteRoundState.rogue/duel_revealed_card),
 *                 phase2/phase2_defs.h, phase2/contract_logic.h, phase2/transmutation_logic.h,
 *                 stdio.h, stdlib.h, string.h
 * @deps-last-changed: 2026-04-05 — Added int hint_idx parameter to sv_play_card_with_transmute()
 * ============================================================ */

#include "server_game.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/clock.h"
#include "core/hand.h"
#include "core/trick.h"
#include "core/card.h"
#include "phase2/phase2_defs.h"
#include "phase2/contract_logic.h"
#include "phase2/transmutation_logic.h"

/* Server-side fallback timeouts: client PASS_*_TIME + 5s grace period */
#define SV_DRAFT_TIMEOUT    20.0f  /* PASS_CONTRACT_TIME (15s) + 5s */
#define SV_PASS_TIMEOUT     25.0f  /* PASS_CARD_PASS_TIME (20s) + 5s */
#define SV_SCORING_TIMEOUT  20.0f  /* max wait for scoring confirmation */

/* ---- Forward declarations for internal helpers ---- */

static void sv_reset_tti(TrickTransmuteInfo *tti);
static void sv_do_pass_phase(ServerGame *sg);
static void sv_do_play_phase(ServerGame *sg);
static void sv_do_scoring(ServerGame *sg);
static void sv_ai_select_pass(GameState *gs, int player_id);
static bool sv_ai_play_card(ServerGame *sg, int player_id);
static bool sv_play_card_with_transmute(ServerGame *sg, int player_id, Card card, int hint_idx);
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

static const char *sv_dir_chat_name(PassDirection dir)
{
    static const char *names[] = {"the left", "the right", "the front", "none"};
    return (dir >= 0 && dir < PASS_COUNT) ? names[dir] : "???";
}

/* Player name resolution — uses names from Room if set, else fallback */
static const char *s_sv_names[NUM_PLAYERS];
static const char *sv_player_name(int id)
{
    static const char *fallback[] = {"Player 0", "Player 1", "Player 2", "Player 3"};
    if (id < 0 || id >= NUM_PLAYERS) return "???";
    return (s_sv_names[id] && s_sv_names[id][0]) ? s_sv_names[id] : fallback[id];
}

/* Push a game event message to the chat queue for broadcast.
 * r,g,b = message color; transmute_id = tooltip (-1 = none);
 * highlight = substring to underline (NULL = none). */
static void sv_push_chat(ServerGame *sg, uint8_t r, uint8_t g, uint8_t b,
                         int16_t transmute_id, const char *highlight,
                         const char *fmt, ...)
    __attribute__((format(printf, 7, 8)));
static void sv_push_chat(ServerGame *sg, uint8_t r, uint8_t g, uint8_t b,
                         int16_t transmute_id, const char *highlight,
                         const char *fmt, ...)
{
    if (sg->chat_count >= SV_CHAT_QUEUE_MAX) return;
    int idx = sg->chat_count;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sg->chat_queue[idx], SV_CHAT_MSG_LEN, fmt, ap);
    va_end(ap);
    sg->chat_colors[idx][0] = r;
    sg->chat_colors[idx][1] = g;
    sg->chat_colors[idx][2] = b;
    sg->chat_transmute_ids[idx] = transmute_id;
    if (highlight && highlight[0]) {
        strncpy(sg->chat_highlights[idx], highlight, 31);
        sg->chat_highlights[idx][31] = '\0';
    } else {
        sg->chat_highlights[idx][0] = '\0';
    }
    sg->chat_count++;
}

/* Color shorthands for sv_push_chat */
#define SV_CLR_GRAY    200, 200, 200
#define SV_CLR_YELLOW  253, 249, 0
#define SV_CLR_MAGENTA 255, 0, 255
#define SV_CLR_RED     230, 41, 55
#define SV_CLR_PURPLE  200, 122, 255
#define SV_CLR_ORANGE  255, 161, 0

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
    sg->last_trick_winner = -1;
    sg->duel_target_player = -1;
    sg->duel_target_hand_index = -1;
    for (int i = 0; i < NUM_PLAYERS; i++)
        sg->selected_transmute_slot[i] = -1;
    sv_reset_tti(&sg->current_tti);
}

void server_game_start(ServerGame *sg)
{
    /* Mark all players as human=false for server (all AI-driven by default).
     * Room layer overrides is_human for connected players after this call. */
    for (int i = 0; i < NUM_PLAYERS; i++) {
        sg->gs.players[i].is_human = false;
    }

    /* Set up name pointers for sv_player_name() */
    for (int i = 0; i < NUM_PLAYERS; i++)
        s_sv_names[i] = sg->player_names[i];

    sg->gs.phase = PHASE_MENU; /* server has no login phase */
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
    for (int i = 0; i < NUM_PLAYERS; i++)
        sg->selected_transmute_slot[i] = -1;

    printf("=== Game Started ===\n");
}

void server_game_tick(ServerGame *sg)
{
    if (!sg->game_active) return;

    switch (sg->gs.phase) {
    case PHASE_DEALING:
#ifdef DEBUG
        /* Debug: skip to round 2 so dealer phase triggers,
         * give seat 0 all transmutations, and give all players 1 Mirror */
        if (sg->gs.round_number == 1) {
            sg->gs.round_number = 2;
            sg->prev_round_points[0] = 10; /* seat 0 becomes dealer */
            int mirror_id = -1;
            for (int t = 0; t < g_transmutation_def_count; t++) {
                transmute_inv_add(&sg->p2.players[0].transmute_inv,
                                  g_transmutation_defs[t].id);
                if (g_transmutation_defs[t].effect == TEFFECT_MIRROR)
                    mirror_id = g_transmutation_defs[t].id;
            }
            if (mirror_id >= 0) {
                for (int i = 1; i < NUM_PLAYERS; i++)
                    transmute_inv_add(&sg->p2.players[i].transmute_inv, mirror_id);
            }
        }
#endif
        /* Instant transition — no deal animation on server */
        printf("\n=== Round %d ===\n", sg->gs.round_number);
        sg->gs.phase = PHASE_PASSING;

        /* Initialize competitive AI state for this hand */
        if (sg->ai_difficulty == 1) {
            for (int i = 0; i < NUM_PLAYERS; i++) {
                if (!sg->gs.players[i].is_human)
                    comp_ai_init_hand(&sg->comp_ai[i], i, &sg->gs);
            }
        }
        contract_round_reset(&sg->p2);
        sg->pass_done = false;
        sg->contracts_done = false;

        /* Initialize pass sub-state machine */
        sg->dealer_player = sv_determine_dealer(sg);
        if (sg->dealer_player >= 0) {
            sv_push_chat(sg, SV_CLR_YELLOW, -1, NULL,
                         "%s lost the round and becomes the dealer!",
                         sv_player_name(sg->dealer_player));
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
        for (int i = 0; i < NUM_PLAYERS; i++)
            sg->selected_transmute_slot[i] = -1;
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

/* Helper: write a player-facing rejection reason into err_out */
#define REJECT(fmt, ...) do { \
    if (err_out) snprintf(err_out, err_len, fmt, ##__VA_ARGS__); \
    return false; \
} while (0)

bool server_game_apply_cmd(ServerGame *sg, int seat, const InputCmd *cmd,
                           char *err_out, size_t err_len)
{
    if (!sg->game_active) REJECT("Game is not active");
    if (seat < 0 || seat >= NUM_PLAYERS) REJECT("Invalid seat");

    GameState *gs = &sg->gs;
    Phase2State *p2 = &sg->p2;

    switch (cmd->type) {

    case INPUT_CMD_DEALER_DIR:
        if (sg->pass_substate != SV_PASS_DEALER_DIR ||
            seat != sg->dealer_player) {
            printf("REJECTED: DEALER_DIR from seat %d (expected dealer %d, state %d)\n",
                   seat, sg->dealer_player, sg->pass_substate);
            REJECT("Not your turn to choose direction");
        }
        {
            int dir = cmd->dealer_dir.direction;
            if (dir < 0 || dir >= 3) {
                printf("REJECTED: invalid direction %d\n", dir);
                REJECT("Invalid pass direction");
            }
            gs->pass_direction = (PassDirection)dir;
            printf("Dealer %s picks direction: %s\n",
                   sv_player_name(seat), sv_dir_name(gs->pass_direction));
            sg->pass_substate = SV_PASS_DEALER_AMT;
            sg->state_dirty = true;
        }
        return true;

    case INPUT_CMD_DEALER_AMT:
        if (sg->pass_substate != SV_PASS_DEALER_AMT ||
            seat != sg->dealer_player) {
            REJECT("Not your turn to choose amount");
        }
        {
            int amt = cmd->dealer_amt.amount;
            if (amt != 0 && (amt < 2 || amt > 4)) {
                printf("REJECTED: invalid pass amount %d\n", amt);
                REJECT("Pass amount must be 0 or 2-4");
            }
            gs->pass_card_count = amt;
            printf("Dealer %s picks amount: %d cards\n",
                   sv_player_name(seat), amt);
            sg->pass_substate = SV_PASS_DEALER_CONFIRM;
            sg->state_dirty = true;
        }
        return true;

    case INPUT_CMD_DEALER_CONFIRM:
        if (sg->pass_substate != SV_PASS_DEALER_CONFIRM ||
            seat != sg->dealer_player) {
            REJECT("Not your turn to confirm");
        }
        printf("Dealer %s confirms\n", sv_player_name(seat));
        /* Override direction when no cards are being passed */
        if (gs->pass_card_count == 0)
            gs->pass_direction = PASS_NONE;
        /* Announce dealer decision */
        if (gs->pass_card_count == 0) {
            sv_push_chat(sg, SV_CLR_YELLOW, -1, NULL,
                         "No passing this round!");
        } else {
            sv_push_chat(sg, SV_CLR_YELLOW, -1, NULL,
                         "Pass %d card%s to %s!",
                         gs->pass_card_count,
                         gs->pass_card_count == 1 ? "" : "s",
                         sv_dir_chat_name(gs->pass_direction));
        }
        sg->pass_substate = SV_PASS_CONTRACT_DRAFT;
        sg->draft_round = 0;
        sg->draft_initialized = false;
        sg->state_dirty = true;
        return true;

    case INPUT_CMD_SELECT_CONTRACT:
        if (sg->pass_substate != SV_PASS_CONTRACT_DRAFT) {
            REJECT("Cannot draft contracts now");
        }
        {
            DraftState *draft = &p2->round.draft;
            if (draft->players[seat].has_picked_this_round) {
                printf("REJECTED: seat %d already picked this draft round\n", seat);
                REJECT("Already drafted this round");
            }
            int pair_idx = cmd->contract.pair_index;
            if (pair_idx < 0 || pair_idx >= draft->players[seat].available_count) {
                printf("REJECTED: invalid pair index %d\n", pair_idx);
                REJECT("Invalid contract choice");
            }
            draft_pick(draft, seat, pair_idx);
            printf("Seat %d drafted contract (pair %d)\n", seat, pair_idx);
        }
        return true;

    case INPUT_CMD_SELECT_CARD:
        if (sg->pass_substate != SV_PASS_CARD_SELECT) {
            REJECT("Cannot select cards now");
        }
        if (gs->pass_ready[seat]) {
            printf("REJECTED: seat %d already submitted pass cards\n", seat);
            REJECT("Pass cards already submitted");
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
                REJECT("Card not in your hand");
            }

            /* Count currently selected cards and check for duplicate */
            int selected_count = 0;
            int found = -1;
            for (int i = 0; i < MAX_PASS_CARD_COUNT; i++) {
                if (i < pc && !card_is_none(gs->pass_selections[seat][i])) {
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
                gs->pass_selections[seat][selected_count - 1] = CARD_NONE;
                selected_count--;
            } else {
                if (selected_count >= pc) {
                    printf("REJECTED: seat %d already selected %d/%d cards\n",
                           seat, selected_count, pc);
                    REJECT("Maximum cards already selected");
                }
                gs->pass_selections[seat][selected_count] = card;
                selected_count++;
            }

            /* Selection tracked; player must send INPUT_CMD_CONFIRM to submit */
        }
        return true;

    case INPUT_CMD_SELECT_TRANSMUTATION:
        if (sg->pass_substate != SV_PASS_CARD_SELECT) {
            REJECT("Cannot select transmutations now");
        }
        {
            int tid = cmd->transmute_select.inv_slot; /* now carries transmutation ID */
            if (tid == -1) {
                sg->selected_transmute_slot[seat] = -1;
                printf("Seat %d deselected transmutation\n", seat);
            } else {
                TransmuteInventory *inv = &p2->players[seat].transmute_inv;
                int found = -1;
                for (int i = 0; i < inv->count; i++) {
                    if (inv->items[i] == tid) { found = i; break; }
                }
                if (found < 0) {
                    REJECT("Transmutation not in inventory");
                }
                sg->selected_transmute_slot[seat] = found;
                printf("Seat %d selected transmutation id=%d slot=%d\n", seat, tid, found);
            }
        }
        return true;

    case INPUT_CMD_APPLY_TRANSMUTATION:
        if (sg->pass_substate != SV_PASS_CARD_SELECT) {
            REJECT("Cannot apply transmutations now");
        }
        {
            /* Client sends card identity because its hand may be reordered
             * differently from the server's sorted hand.  Find the card
             * by identity to get the correct server-side index. */
            Card target_card = cmd->transmute_apply.card;
            int hand_idx = -1;
            for (int i = 0; i < gs->players[seat].hand.count; i++) {
                if (card_equals(gs->players[seat].hand.cards[i], target_card)) {
                    hand_idx = i;
                    break;
                }
            }
            int sel_slot = sg->selected_transmute_slot[seat];
            if (sel_slot < 0) {
                REJECT("No transmutation selected");
            }
            TransmuteInventory *inv = &p2->players[seat].transmute_inv;
            if (sel_slot >= inv->count) {
                REJECT("Invalid inventory slot");
            }
            if (hand_idx < 0) {
                REJECT("Card not in hand");
            }
            int tmut_id = inv->items[sel_slot];
            Card old_card = gs->players[seat].hand.cards[hand_idx];
            if (!transmute_apply(&gs->players[seat].hand,
                                 &p2->players[seat].hand_transmutes,
                                 inv, hand_idx, tmut_id, seat)) {
                REJECT("Cannot apply that transmutation");
            }
            /* Update pass selection to reflect new card identity */
            Card new_card = gs->players[seat].hand.cards[hand_idx];
            for (int i = 0; i < gs->pass_card_count; i++) {
                if (!card_is_none(gs->pass_selections[seat][i]) &&
                    card_equals(gs->pass_selections[seat][i], old_card)) {
                    gs->pass_selections[seat][i] = new_card;
                    break;
                }
            }
            printf("Seat %d applied transmutation %d to hand card %d\n",
                   seat, tmut_id, hand_idx);
            sg->selected_transmute_slot[seat] = -1;
            sg->state_dirty = true;
        }
        return true;

    case INPUT_CMD_CONFIRM:
        /* Scoring phase: mark player as ready to advance */
        if (gs->phase == PHASE_SCORING && sg->scoring_evaluated) {
            sg->scoring_ready[seat] = true;
            sg->state_dirty = true;
            return true;
        }
        if (sg->pass_substate != SV_PASS_CARD_SELECT) {
            return false; /* silent ignore outside card select */
        }
        if (gs->pass_ready[seat]) {
            return true; /* already submitted, no-op */
        }
        {
            int pc = gs->pass_card_count;
            int selected_count = 0;
            for (int i = 0; i < pc; i++) {
                if (!card_is_none(gs->pass_selections[seat][i]))
                    selected_count++;
            }
            if (selected_count != pc) {
                REJECT("Select your pass cards first");
            }
            Card pass_cards[MAX_PASS_CARD_COUNT];
            for (int i = 0; i < pc; i++) {
                pass_cards[i] = gs->pass_selections[seat][i];
            }
            game_state_select_pass(gs, seat, pass_cards, pc);
            printf("Seat %d confirmed %d pass cards\n", seat, pc);
        }
        return true;

    case INPUT_CMD_PLAY_CARD:
        if (sg->play_substate != SV_PLAY_WAIT_TURN) {
            REJECT("Cannot play cards now");
        }
        if (game_state_current_player(gs) != seat) {
            printf("REJECTED: PLAY_CARD from seat %d, not their turn (current: %d)\n",
                   seat, game_state_current_player(gs));
            REJECT("Not your turn");
        }
        {
            Card card = cmd->card.card;

            /* Record hearts_broken state on first card of trick */
            if (gs->current_trick.num_played == 0) {
                sg->hearts_broken_at_trick_start = gs->hearts_broken;
            }

            if (!sv_play_card_with_transmute(sg, seat, card, cmd->card.card_index)) {
                printf("REJECTED: illegal play %s from seat %d\n",
                       card_name(card), seat);
                REJECT("That card cannot be played");
            }
            sv_log_play(sg, seat);

            /* If trick is now complete, broadcast num_played=4 before resolving */
            if (trick_is_complete(&gs->current_trick)) {
                sg->play_substate = SV_PLAY_TRICK_BROADCAST;
            }
        }
        return true;

    case INPUT_CMD_ROGUE_REVEAL:
        if (sg->play_substate != SV_PLAY_ROGUE_WAIT) {
            REJECT("Cannot use Rogue now");
        }
        if (seat != p2->round.transmute_round.rogue_pending_winner) {
            REJECT("Cannot use Rogue now");
        }
        {
            int target = cmd->rogue_reveal.target_player;
            if (target < 0 || target >= NUM_PLAYERS || target == seat) {
                REJECT("Invalid Rogue target");
            }
            if (gs->players[target].hand.count <= 0) {
                REJECT("Target has no cards");
            }

            /* Validate and filter by chosen suit */
            int suit = cmd->rogue_reveal.suit;
            if (suit < 0 || suit >= SUIT_COUNT) {
                REJECT("Invalid Rogue suit");
            }

            int count = 0;
            for (int i = 0; i < gs->players[target].hand.count; i++) {
                if ((int)gs->players[target].hand.cards[i].suit == suit) {
                    p2->round.transmute_round.rogue_revealed_cards[count++] =
                        gs->players[target].hand.cards[i];
                }
            }
            p2->round.transmute_round.rogue_chosen_suit = suit;
            p2->round.transmute_round.rogue_chosen_target = target;
            p2->round.transmute_round.rogue_revealed_count = count;

            printf("  [Rogue] %s reveals %s's %d card(s) of suit %d\n",
                   sv_player_name(seat), sv_player_name(target), count, suit);

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
            sg->state_dirty = true;
        }
        return true;

    case INPUT_CMD_DUEL_PICK:
        if (sg->play_substate != SV_PLAY_DUEL_PICK_WAIT) {
            REJECT("Cannot pick Duel target now");
        }
        if (seat != p2->round.transmute_round.duel_pending_winner) {
            REJECT("Cannot pick Duel target now");
        }
        {
            int target = cmd->duel_pick.target_player;
            if (target < 0 || target >= NUM_PLAYERS || target == seat) {
                REJECT("Invalid Duel target");
            }
            if (gs->players[target].hand.count <= 0) {
                REJECT("Target has no cards");
            }

            /* Server randomly picks which card to take */
            int hidx = rand() % gs->players[target].hand.count;

            /* Store target for DUEL_GIVE step */
            sg->duel_target_player = target;
            sg->duel_target_hand_index = hidx;

            /* Broadcast chosen card info to client */
            p2->round.transmute_round.duel_chosen_card_idx = hidx;
            p2->round.transmute_round.duel_chosen_target = target;
            p2->round.transmute_round.duel_revealed_card =
                gs->players[target].hand.cards[hidx];
            sg->play_substate = SV_PLAY_DUEL_GIVE_WAIT;
            sg->state_dirty = true;
        }
        return true;

    case INPUT_CMD_DUEL_GIVE:
        if (sg->play_substate != SV_PLAY_DUEL_GIVE_WAIT) {
            REJECT("Cannot give Duel card now");
        }
        if (seat != p2->round.transmute_round.duel_pending_winner) {
            REJECT("Cannot give Duel card now");
        }
        {
            int own_idx = cmd->duel_give.hand_index;
            if (own_idx < 0 || own_idx >= gs->players[seat].hand.count) {
                REJECT("Invalid card to give");
            }

            /* Validate stored target is still valid */
            int target = sg->duel_target_player;
            int target_idx = sg->duel_target_hand_index;
            if (target < 0 || target >= NUM_PLAYERS ||
                target_idx < 0 || target_idx >= gs->players[target].hand.count) {
                printf("REJECTED: duel target no longer valid\n");
                p2->round.transmute_round.duel_pending_winner = -1;
                sg->play_substate = SV_PLAY_WAIT_TURN;
                REJECT("Duel target is no longer valid");
            }

            transmute_swap_between_players(gs, p2, seat, own_idx,
                                           target, target_idx);
            printf("  [Duel] %s swaps a card with %s\n",
                   sv_player_name(seat), sv_player_name(target));

            p2->round.transmute_round.duel_pending_winner = -1;
            p2->round.transmute_round.duel_chosen_card_idx = -1;
            p2->round.transmute_round.duel_chosen_target = -1;
            sg->duel_target_player = -1;
            sg->duel_target_hand_index = -1;
            sg->play_substate = SV_PLAY_WAIT_TURN;
            sg->state_dirty = true;
        }
        return true;

    case INPUT_CMD_DUEL_RETURN:
        if (sg->play_substate != SV_PLAY_DUEL_GIVE_WAIT) {
            REJECT("Cannot return Duel card now");
        }
        if (seat != p2->round.transmute_round.duel_pending_winner) {
            REJECT("Cannot return Duel card now");
        }
        /* Player chose to return the opponent's card instead of swapping.
         * Clear duel state and resume normal play. */
        printf("  [Duel] %s returns the card (no swap)\n",
               sv_player_name(seat));
        p2->round.transmute_round.duel_pending_winner = -1;
        p2->round.transmute_round.duel_chosen_card_idx = -1;
        p2->round.transmute_round.duel_chosen_target = -1;
        sg->duel_target_player = -1;
        sg->duel_target_hand_index = -1;
        sg->play_substate = SV_PLAY_WAIT_TURN;
        sg->state_dirty = true;
        return true;

    default:
        printf("REJECTED: unhandled command type %d from seat %d\n",
               cmd->type, seat);
        REJECT("Unknown command");
    }
}

#undef REJECT

/* ================================================================
 * Pass Phase — Sub-state machine
 * ================================================================ */

static void sv_do_pass_phase(ServerGame *sg)
{
    GameState *gs = &sg->gs;
    Phase2State *p2 = &sg->p2;

    sg->pass_phase_timer += FIXED_DT;

    switch (sg->pass_substate) {

    case SV_PASS_DEALER_DIR:
        if (!gs->players[sg->dealer_player].is_human) {
            /* AI dealer picks direction */
            if (sg->ai_difficulty == 1)
                gs->pass_direction = comp_ai_pick_direction(
                    &sg->comp_ai[sg->dealer_player], gs);
            else
                gs->pass_direction = (PassDirection)(rand() % 3);
            printf("Dealer: %s — %s\n",
                   sv_player_name(sg->dealer_player),
                   sv_dir_name(gs->pass_direction));
            sg->pass_substate = SV_PASS_DEALER_AMT;
            sg->state_dirty = true;
        }
        /* Human: wait for DEALER_DIR command */
        break;

    case SV_PASS_DEALER_AMT:
        if (!gs->players[sg->dealer_player].is_human) {
            if (sg->ai_difficulty == 1)
                gs->pass_card_count = comp_ai_pick_amount(
                    &sg->comp_ai[sg->dealer_player], gs);
            else {
                static const int amounts[] = {2, 3, 4};
                gs->pass_card_count = amounts[rand() % 3];
            }
            printf("Dealer: %s — %d cards\n",
                   sv_player_name(sg->dealer_player),
                   gs->pass_card_count);
            sg->pass_substate = SV_PASS_DEALER_CONFIRM;
            sg->state_dirty = true;
        }
        break;

    case SV_PASS_DEALER_CONFIRM:
        if (!gs->players[sg->dealer_player].is_human) {
            /* Override direction when no cards are being passed */
            if (gs->pass_card_count == 0)
                gs->pass_direction = PASS_NONE;
            /* Announce AI dealer decision */
            if (gs->pass_card_count == 0) {
                sv_push_chat(sg, SV_CLR_YELLOW, -1, NULL,
                             "No passing this round!");
            } else {
                sv_push_chat(sg, SV_CLR_YELLOW, -1, NULL,
                             "Pass %d card%s to %s!",
                             gs->pass_card_count,
                             gs->pass_card_count == 1 ? "" : "s",
                             sv_dir_chat_name(gs->pass_direction));
            }
            sg->pass_substate = SV_PASS_CONTRACT_DRAFT;
            sg->draft_round = 0;
            sg->draft_initialized = false;
            sg->pass_phase_timer = 0.0f;
            sg->state_dirty = true;
        }
        break;

    case SV_PASS_CONTRACT_DRAFT: {
        DraftState *draft = &p2->round.draft;

        /* Initialize draft on first entry */
        if (!sg->draft_initialized) {
            draft_generate_pool(draft);
            sg->draft_initialized = true;
            sg->draft_round = 0;
            sg->state_dirty = true;
        }

        /* AI players pick automatically */
        for (int p = 0; p < NUM_PLAYERS; p++) {
            if (!gs->players[p].is_human && !draft->players[p].has_picked_this_round) {
                if (sg->ai_difficulty == 1) {
                    int idx = comp_ai_draft_pick(&draft->players[p], p2);
                    draft_pick(draft, p, idx);
                } else {
                    draft_ai_pick(draft, p);
                }
            }
        }

        /* Server fallback: auto-pick for humans after grace period */
        if (sg->pass_phase_timer > SV_DRAFT_TIMEOUT) {
            for (int p = 0; p < NUM_PLAYERS; p++) {
                if (gs->players[p].is_human && !draft->players[p].has_picked_this_round) {
                    draft_pick(draft, p, 0);
                    printf("Server timeout: auto-picked contract for seat %d\n", p);
                }
            }
        }

        /* Check if all players have picked this round */
        if (draft_all_picked(draft)) {
            sg->draft_round++;
            sg->pass_phase_timer = 0.0f; /* reset timer for next round */
            if (sg->draft_round < DRAFT_ROUNDS) {
                draft_advance_round(draft);
                sg->state_dirty = true;
            } else {
                draft_finalize(draft, p2);
                printf("Contracts drafted for all players\n");
                sg->contracts_done = true;
                sg->pass_substate = SV_PASS_CARD_SELECT;
                sg->pass_phase_timer = 0.0f;
                /* Random 3-6s delay per AI for natural pass timing */
                for (int i = 0; i < NUM_PLAYERS; i++) {
                    if (!gs->players[i].is_human)
                        sg->ai_pass_delay[i] = 3.0f + (float)(rand() % 3001) / 1000.0f;
                    else
                        sg->ai_pass_delay[i] = 0.0f;
                }
                sg->state_dirty = true;
            }
        }
        /* If humans haven't picked yet, wait */
        break;
    }

    case SV_PASS_CARD_SELECT:
        /* AI players select pass cards */
        for (int p = 0; p < NUM_PLAYERS; p++) {
            if (!gs->players[p].is_human && !gs->pass_ready[p] &&
                sg->pass_phase_timer >= sg->ai_pass_delay[p]) {
                if (sg->ai_difficulty == 1) {
                    int pc = gs->pass_card_count;
                    Card pass_cards[MAX_PASS_CARD_COUNT];
                    comp_ai_select_pass(&sg->comp_ai[p], gs, pass_cards, pc);
                    game_state_select_pass(gs, p, pass_cards, pc);
                } else {
                    sv_ai_select_pass(gs, p);
                }
            }
        }

        /* Server fallback: auto-select pass cards for humans after grace period */
        if (sg->pass_phase_timer > SV_PASS_TIMEOUT) {
            for (int p = 0; p < NUM_PLAYERS; p++) {
                if (gs->players[p].is_human && !gs->pass_ready[p]) {
                    sv_ai_select_pass(gs, p);
                    printf("Server timeout: auto-selected pass cards for seat %d\n", p);
                }
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

        /* Save transmute state before pass (hand indices will shift) */
        typedef struct {
            int  tid;
            Card orig;
            Card transmuted;
            int  transmuter_player;
            bool fogged;
            int  fog_transmuter;
            bool is_passed;
        } SavedTransmute;
        SavedTransmute saved[NUM_PLAYERS][MAX_HAND_SIZE];
        int saved_count[NUM_PLAYERS] = {0};

        if (p2->enabled) {
            for (int pl = 0; pl < NUM_PLAYERS; pl++) {
                Hand *hand = &gs->players[pl].hand;
                HandTransmuteState *hts = &p2->players[pl].hand_transmutes;
                for (int k = 0; k < hand->count; k++) {
                    if (!transmute_is_transmuted(hts, k)) continue;
                    int si = saved_count[pl]++;
                    saved[pl][si].tid = hts->slots[k].transmutation_id;
                    saved[pl][si].orig = hts->slots[k].original_card;
                    saved[pl][si].transmuted = hand->cards[k];
                    saved[pl][si].transmuter_player =
                        hts->slots[k].transmuter_player;
                    saved[pl][si].fogged = hts->slots[k].fogged;
                    saved[pl][si].fog_transmuter =
                        hts->slots[k].fog_transmuter;
                    saved[pl][si].is_passed = false;
                    for (int j = 0; j < gs->pass_card_count; j++) {
                        if (card_equals(hand->cards[k],
                                        gs->pass_selections[pl][j])) {
                            saved[pl][si].is_passed = true;
                            break;
                        }
                    }
                }
            }
        }

        game_state_execute_pass(gs);

        /* Update competitive AI pass tracking */
        if (sg->ai_difficulty == 1) {
            for (int p = 0; p < NUM_PLAYERS; p++) {
                if (!gs->players[p].is_human) {
                    int target = (p + offset) % NUM_PLAYERS;
                    int source = (p + NUM_PLAYERS - offset) % NUM_PLAYERS;
                    comp_ai_on_pass_execute(
                        &sg->comp_ai[p],
                        gs->pass_selections[p], gs->pass_card_count,
                        gs->pass_selections[source], gs->pass_card_count,
                        gs->pass_direction, target);
                }
            }
        }

        /* Restore transmute state to correct hand positions */
        if (p2->enabled) {
            for (int pl = 0; pl < NUM_PLAYERS; pl++)
                transmute_hand_init(&p2->players[pl].hand_transmutes);
            for (int pl = 0; pl < NUM_PLAYERS; pl++) {
                for (int si = 0; si < saved_count[pl]; si++) {
                    if (saved[pl][si].tid < 0) continue;
                    int owner = saved[pl][si].is_passed
                        ? (pl + offset) % NUM_PLAYERS : pl;
                    Hand *ohand = &gs->players[owner].hand;
                    HandTransmuteState *ohts =
                        &p2->players[owner].hand_transmutes;
                    Card tc = saved[pl][si].transmuted;
                    for (int k = 0; k < ohand->count; k++) {
                        if (card_equals(ohand->cards[k], tc) &&
                            !transmute_is_transmuted(ohts, k)) {
                            ohts->slots[k].transmutation_id =
                                saved[pl][si].tid;
                            ohts->slots[k].original_card =
                                saved[pl][si].orig;
                            ohts->slots[k].transmuter_player =
                                saved[pl][si].transmuter_player;
                            ohts->slots[k].fogged =
                                saved[pl][si].fogged;
                            ohts->slots[k].fog_transmuter =
                                saved[pl][si].fog_transmuter;
                            break;
                        }
                    }
                }
            }
        }
        printf("Pass complete\n");

        sg->pass_substate = SV_PASS_TRANSMUTE;
        break;
    }

    case SV_PASS_TRANSMUTE:
        /* Apply AI transmutations for non-human players */
        for (int p = 0; p < NUM_PLAYERS; p++) {
            if (!gs->players[p].is_human) {
                if (sg->ai_difficulty == 1) {
                    comp_ai_apply_transmutations(
                        &sg->comp_ai[p],
                        &gs->players[p].hand,
                        &p2->players[p].hand_transmutes,
                        &p2->players[p].transmute_inv,
                        false, p);
                } else {
                    transmute_ai_apply(&gs->players[p].hand,
                                       &p2->players[p].hand_transmutes,
                                       &p2->players[p].transmute_inv,
                                       false, p);
                }
            }
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
            sg->play_substate = SV_PLAY_TRICK_BROADCAST;
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

        /* AI think delay: 0-1s before playing */
        if (sg->ai_play_delay < 0.0f) {
            sg->ai_play_delay = (float)(rand() % 1001) / 1000.0f;
            sg->ai_play_timer = 0.0f;
        }
        sg->ai_play_timer += FIXED_DT;
        if (sg->ai_play_timer < sg->ai_play_delay) break;
        sg->ai_play_delay = -1.0f; /* reset for next AI turn */

        /* AI plays */
        bool ai_played = false;
        if (sg->ai_difficulty == 1) {
            Card chosen = comp_ai_play_card(&sg->comp_ai[current], sg, current);
            ai_played = sv_play_card_with_transmute(sg, current, chosen, -1);
            if (!ai_played) {
                fprintf(stderr, "WARN: competitive AI card rejected, falling back\n");
                ai_played = sv_ai_play_card(sg, current);
            }
        } else {
            ai_played = sv_ai_play_card(sg, current);
        }
        if (!ai_played) {
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

        /* If trick now complete, broadcast num_played=4 before resolving */
        if (trick_is_complete(&gs->current_trick)) {
            sg->play_substate = SV_PLAY_TRICK_BROADCAST;
        }
        break;
    }

    case SV_PLAY_TRICK_BROADCAST:
        /* One tick delay: let the next broadcast send num_played=4 before resolving */
        sg->play_substate = SV_PLAY_TRICK_DONE;
        break;

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

    /* Log transmutation card play for clients */
    if (sg->current_tti.transmutation_ids[slot] >= 0 &&
        !sg->current_tti.fogged[slot]) {
        int tid = sg->current_tti.transmutation_ids[slot];
        const TransmutationDef *td = phase2_get_transmutation(tid);
        if (td) {
            sv_push_chat(sg, SV_CLR_PURPLE, (int16_t)tid, td->name,
                         "%s plays %s!",
                         sv_player_name(player_id), td->name);
        }
    } else if (sg->current_tti.fogged[slot]) {
        sv_push_chat(sg, SV_CLR_PURPLE, -1, NULL,
                     "[Fog] A hidden card was played");
    }
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
        sg->last_trick_winner = winner;
        int points = transmute_trick_count_points(&saved_trick, &saved_tti, p2);

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

            sv_push_chat(sg, SV_CLR_GRAY, -1, NULL,
                         "%s took trick %d",
                         sv_player_name(winner), gs->tricks_played);

            /* Stat tracking: tricks won, hearts collected, QoS caught */
            sg->stat_tricks_won[winner]++;
            for (int si = 0; si < CARDS_PER_TRICK; si++) {
                if (saved_trick.cards[si].suit == SUIT_HEARTS)
                    sg->stat_hearts_collected[winner]++;
                if (saved_trick.cards[si].suit == SUIT_SPADES &&
                    saved_trick.cards[si].rank == RANK_Q)
                    sg->stat_qos_caught[winner]++;
            }

            /* Log QoS hit */
            for (int qi = 0; qi < CARDS_PER_TRICK; qi++) {
                if (saved_trick.cards[qi].suit == SUIT_SPADES &&
                    saved_trick.cards[qi].rank == RANK_Q) {
                    int played_by = saved_trick.player_ids[qi];
                    const char *qname = "Queen of Spades";
                    if (saved_tti.transmutation_ids[qi] >= 0) {
                        const TransmutationDef *td =
                            phase2_get_transmutation(
                                saved_tti.transmutation_ids[qi]);
                        if (td) qname = td->name;
                    }
                    if (played_by == winner) {
                        sv_push_chat(sg, SV_CLR_RED, -1, NULL,
                                     "%s took their own %s!",
                                     sv_player_name(played_by), qname);
                    } else {
                        sv_push_chat(sg, SV_CLR_RED, -1, NULL,
                                     "%s hit %s with %s!",
                                     sv_player_name(played_by),
                                     sv_player_name(winner), qname);
                    }
                    break;
                }
            }

            contract_on_trick_complete(p2, gs, &saved_trick, winner,
                                       gs->tricks_played - 1,
                                       &saved_tti,
                                       sg->hearts_broken_at_trick_start);
            transmute_on_trick_complete(p2, &saved_trick, winner, &saved_tti);

            /* Chat messages for round-end transmutation effects */
            if (p2->round.transmute_round.martyr_flags[winner] > 0) {
                int mc = p2->round.transmute_round.martyr_flags[winner];
                int mult = 1 << mc;
                sv_push_chat(sg, SV_CLR_PURPLE, -1, NULL,
                             "The Martyr: %s's points x%d at the end of the round!",
                             sv_player_name(winner), mult);
            }

            /* Consume binding flags */
            for (int bi = 0; bi < NUM_PLAYERS; bi++) {
                if (p2->binding_auto_win[bi]) {
                    p2->binding_auto_win[bi] = 0;
                }
            }
        }
    } else {
        /* Vanilla Hearts — no Phase 2 */
        Trick pre_trick = gs->current_trick;
        winner = trick_get_winner(&gs->current_trick);
        sg->last_trick_winner = winner;
        int points = trick_count_points(&gs->current_trick);
        game_state_complete_trick(gs);
        if (winner >= 0) {
            printf("  -> %s takes trick (%d pts)\n",
                   sv_player_name(winner), points);

            sv_push_chat(sg, SV_CLR_GRAY, -1, NULL,
                         "%s took trick %d",
                         sv_player_name(winner), gs->tricks_played);

            /* Stat tracking: tricks won, hearts collected, QoS caught */
            sg->stat_tricks_won[winner]++;
            for (int si = 0; si < CARDS_PER_TRICK; si++) {
                if (pre_trick.cards[si].suit == SUIT_HEARTS)
                    sg->stat_hearts_collected[winner]++;
                if (pre_trick.cards[si].suit == SUIT_SPADES &&
                    pre_trick.cards[si].rank == RANK_Q)
                    sg->stat_qos_caught[winner]++;
            }

            /* Log QoS hit */
            for (int qi = 0; qi < CARDS_PER_TRICK; qi++) {
                if (pre_trick.cards[qi].suit == SUIT_SPADES &&
                    pre_trick.cards[qi].rank == RANK_Q) {
                    int played_by = pre_trick.player_ids[qi];
                    if (played_by == winner) {
                        sv_push_chat(sg, SV_CLR_RED, -1, NULL,
                                     "%s took their own Queen of Spades!",
                                     sv_player_name(played_by));
                    } else {
                        sv_push_chat(sg, SV_CLR_RED, -1, NULL,
                                     "%s hit %s with Queen of Spades!",
                                     sv_player_name(played_by),
                                     sv_player_name(winner));
                    }
                    break;
                }
            }
        }
    }



    /* Update competitive AI state */
    if (sg->ai_difficulty == 1) {
        Trick resolved = gs->current_trick;
        for (int p = 0; p < NUM_PLAYERS; p++) {
            if (!gs->players[p].is_human)
                comp_ai_on_trick_complete(&sg->comp_ai[p], &resolved, winner);
        }
    }

    /* Reset TTI for next trick */
    sv_reset_tti(&sg->current_tti);

    /* Handle post-trick effects (rogue, duel) or advance */
    sv_check_post_trick_effects(sg, winner);
    sg->state_dirty = true;
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
        if (sg->ai_difficulty == 1) {
            int tgt = -1, tsuit = -1;
            comp_ai_rogue_pick(&sg->comp_ai[rw], gs, rw, &tgt, &tsuit);
            if (tgt >= 0 && tsuit >= 0 && gs->players[tgt].hand.count > 0) {
                /* Filter and store revealed cards */
                int count = 0;
                for (int i = 0; i < gs->players[tgt].hand.count; i++) {
                    if ((int)gs->players[tgt].hand.cards[i].suit == tsuit)
                        p2->round.transmute_round.rogue_revealed_cards[count++] =
                            gs->players[tgt].hand.cards[i];
                }
                p2->round.transmute_round.rogue_chosen_suit = tsuit;
                p2->round.transmute_round.rogue_chosen_target = tgt;
                p2->round.transmute_round.rogue_revealed_count = count;
                printf("  [Rogue-Comp] %s reveals %s's %d card(s) of suit %d\n",
                       sg->player_names[rw], sg->player_names[tgt], count, tsuit);
            }
        } else {
            sv_execute_rogue_ai(sg, rw);
        }
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
        if (sg->ai_difficulty == 1) {
            int tgt = -1, tidx = -1, gidx = -1;
            comp_ai_duel_pick(&sg->comp_ai[dw], gs, p2, dw,
                              &tgt, &tidx, &gidx);
            if (tgt >= 0 && tidx >= 0 && gidx >= 0) {
                transmute_swap_between_players(gs, p2, dw, gidx, tgt, tidx);
                printf("  [Duel-Comp] %s swaps with %s\n",
                       sg->player_names[dw], sg->player_names[tgt]);
            }
        } else {
            sv_execute_duel_ai(sg, dw);
        }
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
    Phase2State *p2 = &sg->p2;

    /* Pick opponent with most cards */
    int target = -1;
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
    if (num_candidates > 0)
        target = candidates[rand() % num_candidates];

    if (target < 0 || gs->players[target].hand.count <= 0)
        return;

    /* Pick random suit */
    int suit = rand() % SUIT_COUNT;

    /* Filter and store revealed cards */
    int count = 0;
    for (int i = 0; i < gs->players[target].hand.count; i++) {
        if ((int)gs->players[target].hand.cards[i].suit == suit)
            p2->round.transmute_round.rogue_revealed_cards[count++] =
                gs->players[target].hand.cards[i];
    }
    p2->round.transmute_round.rogue_chosen_suit = suit;
    p2->round.transmute_round.rogue_chosen_target = target;
    p2->round.transmute_round.rogue_revealed_count = count;

    printf("  [Rogue-AI] %s reveals %s's %d card(s) of suit %d\n",
           sv_player_name(winner), sv_player_name(target), count, suit);
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

    /* Two-tick SCORING: tick 1 evaluates and broadcasts results,
     * tick 2 advances to DEALING. This gives clients time to
     * receive the evaluated contract data before the phase changes. */
    if (!sg->scoring_evaluated) {
        /* ---- Tick 1: evaluate, stay in SCORING ---- */

        /* Save round points before they get zeroed */
        for (int i = 0; i < NUM_PLAYERS; i++) {
            sg->prev_round_points[i] = gs->players[i].round_points;
        }

        /* Contract evaluation and rewards */
        if (p2->enabled) {
            for (int p = 0; p < NUM_PLAYERS; p++) {
                contract_evaluate_all(p2, gs, p);
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

        /* Print total scores */
        printf("Total scores:   [");
        for (int i = 0; i < NUM_PLAYERS; i++) {
            printf("%d%s", gs->players[i].total_score,
                   i < NUM_PLAYERS - 1 ? ", " : "");
        }
        printf("]\n");

        /* Stat tracking: moon shots, perfect rounds, contracts fulfilled */
        for (int i = 0; i < NUM_PLAYERS; i++) {
            int rp = gs->players[i].round_points;
            if (rp == 0)
                sg->stat_perfect_rounds[i]++;
            /* Moon shot: player took all 26 points (before redistribution).
             * In standard Hearts, shooting the moon means round_points == 26
             * and all others get 26 added. Check saved pre-evaluation points. */
            if (sg->prev_round_points[i] == 26)
                sg->stat_moon_shots[i]++;
        }
        if (p2->enabled) {
            for (int i = 0; i < NUM_PLAYERS; i++) {
                for (int c = 0; c < p2->players[i].num_active_contracts; c++) {
                    if (p2->players[i].contracts[c].completed)
                        sg->stat_contracts_fulfilled[i]++;
                }
            }
        }

        sg->scoring_evaluated = true;
        memset(sg->scoring_ready, 0, sizeof(sg->scoring_ready));
        sg->scoring_wait_timer = 0.0f;
        sg->state_dirty = true;
        return; /* Stay in SCORING — broadcast evaluated results */
    }

    /* ---- Wait for all players to confirm before advancing ---- */
    sg->scoring_wait_timer += FIXED_DT;

    bool all_ready = true;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (!sg->scoring_ready[i]) { all_ready = false; break; }
    }

    if (!all_ready && sg->scoring_wait_timer < SV_SCORING_TIMEOUT)
        return; /* Keep waiting */

    if (!all_ready) {
        printf("Server timeout: force-advancing scoring\n");
    }

    /* ---- All ready (or timed out): advance to next round or game over ---- */
    game_state_advance_scoring(gs);

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
    sg->scoring_evaluated = false;
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
            return sv_play_card_with_transmute(sg, player_id, hand->cards[i], -1);
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
                        const TransmutationDef *td_chk = phase2_get_transmutation(tid);
                        if (!td_chk || td_chk->effect != TEFFECT_MIRROR)
                            p2->last_played_transmuted_card = card;
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
static bool sv_play_card_with_transmute(ServerGame *sg, int player_id, Card card, int hint_idx)
{
    GameState *gs = &sg->gs;
    Phase2State *p2 = &sg->p2;
    TrickTransmuteInfo *tti = &sg->current_tti;

    if (!p2->enabled) {
        return game_state_play_card(gs, player_id, card);
    }

    Hand *hand = &gs->players[player_id].hand;
    HandTransmuteState *hts = &p2->players[player_id].hand_transmutes;

    /* Use client hint index if valid, otherwise fall back to first match.
     * This disambiguates duplicate suit/rank (transmuted vs regular). */
    int hand_idx = -1;
    if (hint_idx >= 0 && hint_idx < hand->count &&
        card_equals(hand->cards[hint_idx], card)) {
        hand_idx = hint_idx;
    } else {
        for (int i = 0; i < hand->count; i++) {
            if (card_equals(hand->cards[i], card)) {
                hand_idx = i;
                break;
            }
        }
    }

    if (hand_idx < 0) return false;  /* Card not in hand */

    int trick_slot = gs->current_trick.num_played;
    int tid = -1;
    bool is_transmuted = transmute_is_transmuted(hts, hand_idx);
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

    if (gs->phase != PHASE_PLAYING) return false;
    if (game_state_current_player(gs) != player_id) return false;
    bool first_trick = (gs->tricks_played == 0);

    if (is_transmuted) {
        bool hb = gs->hearts_broken || (leading && p2->curse_force_hearts[player_id]);
        if (!transmute_is_valid_play(&gs->current_trick, hand, hts,
                                     hand_idx, card, hb, first_trick)) {
            return false;
        }
    } else {
        bool was_broken = gs->hearts_broken;
        if (leading && p2->curse_force_hearts[player_id]) {
            gs->hearts_broken = true;
        }
        if (!trick_is_valid_play(&gs->current_trick, hand, card,
                                 gs->hearts_broken, first_trick)) {
            gs->hearts_broken = was_broken;
            return false;
        }
    }

    hand_remove_at(hand, hand_idx);
    trick_play_card(&gs->current_trick, player_id, card);
    if (card.suit == SUIT_HEARTS) {
        gs->hearts_broken = true;
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
            const TransmutationDef *td_chk = phase2_get_transmutation(tid);
            if (!td_chk || td_chk->effect != TEFFECT_MIRROR)
                p2->last_played_transmuted_card = card;
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
