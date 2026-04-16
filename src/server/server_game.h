/* ============================================================
 * @deps-exports: ServerGame (includes ai_play_delay, ai_play_timer,
 *                turn_timer, turn_timer_player,
 *                ai_difficulty, comp_ai[NUM_PLAYERS]),
 *                ServerPassSubstate, ServerPlaySubstate,
 *                server_game_init(), server_game_start(),
 *                server_game_tick(), server_game_is_over()
 * @deps-requires: core/game_state.h (GameState), core/game_mode.h (GameMode),
 *                 core/input_cmd.h (InputCmd),
 *                 phase2/phase2_state.h (Phase2State), phase2/transmutation.h,
 *                 server/ai_competitive.h (CompetitiveAIState)
 * @deps-used-by: server_game.c, server_main.c, server_net.c, room.h
 * @deps-last-changed: 2026-04-15 — vanilla_plan.md Step 2: include core/game_mode.h for GameMode/gamemode_uses_phase2
 * ============================================================ */

#ifndef SERVER_GAME_H
#define SERVER_GAME_H

#include <stdbool.h>
#include <stddef.h>

#include "core/game_state.h"
#include "core/game_mode.h"
#include "core/input_cmd.h"
#include "phase2/phase2_state.h"
#include "server/ai_competitive.h"
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
    SV_PLAY_TRICK_BROADCAST,   /* Trick complete, broadcast num_played=4 first */
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
    bool                scoring_evaluated; /* two-tick SCORING: true after contracts evaluated */
    bool                scoring_ready[NUM_PLAYERS]; /* per-player "done viewing scores" */
    float               scoring_wait_timer;         /* timeout for unresponsive players */

    /* Duel target storage (between DUEL_PICK and DUEL_GIVE) */
    int                 duel_target_player;     /* opponent selected in DUEL_PICK */
    int                 duel_target_hand_index;  /* opponent card index */
    float               duel_timer;             /* timeout for duel pick/give phases */
    bool                duel_ai_swap;           /* bot will swap (vs return) */
    int                 duel_ai_give_idx;       /* card index bot will give */

    /* Last trick winner (for client Roulette determinism) */
    int                 last_trick_winner; /* -1 = no trick completed yet */

    /* Transmutation selection (during SV_PASS_CARD_SELECT) */
    int                 selected_transmute_slot[NUM_PLAYERS]; /* inv slot, -1 = none */

    /* Pass/draft phase timer (server-side fallback for stuck/disconnected clients) */
    float               pass_phase_timer;

    /* Per-AI random delay before confirming pass cards (3-6s) */
    float               ai_pass_delay[NUM_PLAYERS];

    /* AI play delay for current turn (0-1s random, reset after each play) */
    float               ai_play_delay;
    float               ai_play_timer;

    /* Human turn timer — authoritative countdown for the player on the clock
     * in SV_PLAY_WAIT_TURN. Reset whenever the active turn changes.
     * turn_timer_seq encodes (tricks_played, num_played, current_player) plus
     * a flag that flips whenever play_substate leaves and re-enters WAIT_TURN,
     * so Rogue/Duel resolutions also force a fresh allotment. -1 = uninit. */
    float               turn_timer;
    int                 turn_timer_seq;

    /* Snapshot of pass_ready[] for detecting per-player confirmations (AI/timeout) */
    bool                prev_pass_ready[NUM_PLAYERS];

    /* AI difficulty: 0=casual, 1=competitive */
    uint8_t             ai_difficulty;
    uint8_t             timer_option;   /* index into TIMER_BONUS_VALUES */
    uint8_t             point_goal_idx; /* index into POINT_GOAL_VALUES */
    uint8_t             gamemode;       /* 0=Transmutations, 1=Vanilla, 2=Dragon Hearts */

    /* Player display names (copied from Room slots at game start) */
    char                player_names[NUM_PLAYERS][32];

    /* Per-player stat accumulators (sent to lobby at game end) */
    uint16_t stat_moon_shots[NUM_PLAYERS];
    uint16_t stat_qos_caught[NUM_PLAYERS];
    uint16_t stat_contracts_fulfilled[NUM_PLAYERS];
    uint16_t stat_perfect_rounds[NUM_PLAYERS];
    uint16_t stat_hearts_collected[NUM_PLAYERS];
    uint16_t stat_tricks_won[NUM_PLAYERS];

    /* Game event log queue — drained by server_net after each tick */
#define SV_CHAT_QUEUE_MAX 8
#define SV_CHAT_MSG_LEN   128
    char                chat_queue[SV_CHAT_QUEUE_MAX][SV_CHAT_MSG_LEN];
    uint8_t             chat_colors[SV_CHAT_QUEUE_MAX][3]; /* r,g,b */
    int16_t             chat_transmute_ids[SV_CHAT_QUEUE_MAX];
    char                chat_highlights[SV_CHAT_QUEUE_MAX][32];
    int                 chat_count;

    /* Competitive AI state (per-player, per-hand) */
    CompetitiveAIState  comp_ai[NUM_PLAYERS];
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
