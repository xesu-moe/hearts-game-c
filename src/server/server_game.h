/* ============================================================
 * @deps-exports: ServerGame (selected_transmute_slot),
 *                ServerPassSubstate,
 *                server_game_init(), server_game_start(),
 *                server_game_tick(), server_game_is_over(),
 *                server_game_apply_cmd()
 * @deps-requires: core/game_state.h (GameState), core/input_cmd.h (InputCmd),
 *                 phase2/phase2_state.h (Phase2State),
 *                 phase2/transmutation.h (TrickTransmuteInfo, TransmuteEffect)
 * @deps-last-changed: 2026-03-27 — Removed SV_PASS_TRANSMUTE_WAIT; transmutations during SV_PASS_CARD_SELECT
 * ============================================================ */

#ifndef SERVER_GAME_H
#define SERVER_GAME_H

#include <stdbool.h>
#include <stddef.h>

#include "core/game_state.h"
#include "core/input_cmd.h"
#include "phase2/phase2_state.h"
#include "phase2/transmutation.h"

/* ================================================================
 * Server-side sub-state machines
 * ================================================================ */

typedef enum ServerPassSubstate {
    SV_PASS_IDLE = 0,          /* Not in pass phase */
    SV_PASS_DEALER_DIR,        /* Wait for dealer to pick direction */
    SV_PASS_DEALER_AMT,        /* Wait for dealer to pick amount */
    SV_PASS_DEALER_CONFIRM,    /* Wait for dealer to confirm */
    SV_PASS_CONTRACT_DRAFT,    /* Wait for humans to draft contracts */
    SV_PASS_CARD_SELECT,       /* Wait for humans to select pass cards */
    SV_PASS_EXECUTE,           /* Execute pass (instant) */
    SV_PASS_TRANSMUTE,         /* Finalize transmutations (instant, AI auto-apply) */
} ServerPassSubstate;

typedef enum ServerPlaySubstate {
    SV_PLAY_IDLE = 0,          /* Not in play phase */
    SV_PLAY_WAIT_TURN,         /* Wait for current player to act */
    SV_PLAY_TRICK_DONE,        /* Trick complete, resolve */
    SV_PLAY_ROGUE_WAIT,        /* Wait for human rogue pick */
    SV_PLAY_DUEL_PICK_WAIT,    /* Wait for human duel opponent pick */
    SV_PLAY_DUEL_GIVE_WAIT,    /* Wait for human duel card give */
} ServerPlaySubstate;

/* ================================================================
 * ServerGame
 * ================================================================ */

typedef struct ServerGame {
    GameState       gs;
    Phase2State     p2;

    /* Trick transmutation tracking (mirrors PlayPhaseState.current_tti) */
    TrickTransmuteInfo current_tti;

    /* Round state */
    int             dealer_player;   /* -1 if round 1 */
    bool            pass_done;
    bool            contracts_done;
    bool            game_active;
    bool            hearts_broken_at_trick_start;

    /* Previous round points for dealer determination */
    int             prev_round_points[NUM_PLAYERS];

    /* State machine sub-states (Step 6) */
    ServerPassSubstate  pass_substate;
    ServerPlaySubstate  play_substate;
    int                 draft_round; /* current draft round 0..DRAFT_ROUNDS-1 */
    bool                draft_initialized; /* has draft_generate_pool been called */
    bool                state_dirty;     /* triggers broadcast for changes invisible to detector */

    /* Duel target storage (between DUEL_PICK and DUEL_GIVE) */
    int                 duel_target_player;     /* opponent selected in DUEL_PICK */
    int                 duel_target_hand_index;  /* opponent card index */

    /* Transmutation selection (during SV_PASS_CARD_SELECT) */
    int                 selected_transmute_slot[NUM_PLAYERS]; /* inv slot, -1 = none */
} ServerGame;

/* ================================================================
 * Public API
 * ================================================================ */

void server_game_init(ServerGame *sg);
void server_game_start(ServerGame *sg);
void server_game_tick(ServerGame *sg);
bool server_game_is_over(const ServerGame *sg);

/* Apply a human player's command. Validates against current sub-state.
 * Returns true if accepted, false if rejected.
 * On rejection, fills err_out (if non-NULL) with a player-facing reason. */
bool server_game_apply_cmd(ServerGame *sg, int seat, const InputCmd *cmd,
                           char *err_out, size_t err_len);

#endif /* SERVER_GAME_H */
