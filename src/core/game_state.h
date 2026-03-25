#ifndef GAME_STATE_H
#define GAME_STATE_H

/* ============================================================
 * @deps-exports: enum GamePhase, enum PassDirection, enum PassSubphase,
 *                struct GameState, game_state_init(), game_state_start_game(),
 *                game_state_reset_to_menu(), game_state_new_round(),
 *                game_state_current_player(), game_state_play_card(),
 *                game_state_complete_trick(), game_state_complete_trick_with(),
 *                game_state_is_valid_play(), game_state_is_game_over(),
 *                game_state_advance_scoring(), game_state_get_winners()
 * @deps-requires: player.h (Player), deck.h (Deck), trick.h (Trick)
 * @deps-used-by: game_state.c, render.h, render.c, ai.h, play_phase.h,
 *                pass_phase.h, pass_phase.c, turn_flow.h, turn_flow.c,
 *                process_input.h, process_input.c, update.h, update.c,
 *                info_sync.h, info_sync.c, phase_transitions.h, main.c
 * @deps-last-changed: 2026-03-22 — Added PASS_SUB_REVEAL to PassSubphase, skip_human_pass_sort to GameState
 * ============================================================ */

#include <stdbool.h>

#include "deck.h"
#include "player.h"
#include "trick.h"

typedef enum GamePhase {
    PHASE_MENU,
    PHASE_DEALING,
    PHASE_PASSING,
    PHASE_PLAYING,
    PHASE_SCORING,
    PHASE_GAME_OVER,
    PHASE_SETTINGS,
    PHASE_LOGIN,       /* Client-only: lobby auth screen (never sent over wire) */
    PHASE_ONLINE_MENU, /* Client-only: online submenu (create/join/queue) */
    PHASE_COUNT
} GamePhase;

/* Returns true for phases that represent active gameplay (not menu/settings/game-over). */
static inline bool is_ingame_phase(GamePhase phase)
{
    return phase == PHASE_DEALING || phase == PHASE_PASSING ||
           phase == PHASE_PLAYING || phase == PHASE_SCORING;
}

typedef enum PassDirection {
    PASS_LEFT   = 0, /* to player (id + 1) % 4 */
    PASS_RIGHT  = 1, /* to player (id + 3) % 4 */
    PASS_ACROSS = 2, /* to player (id + 2) % 4 */
    PASS_NONE   = 3, /* no passing this round */
    PASS_COUNT  = 4
} PassDirection;

typedef enum PassSubphase {
    PASS_SUB_DEALER     = 0,  /* dealer picks direction + amount */
    PASS_SUB_CONTRACT   = 1,
    PASS_SUB_CARD_PASS  = 2,
    PASS_SUB_TOSS_ANIM  = 3,  /* cards flying to staging area */
    PASS_SUB_TOSS_WAIT  = 4,  /* cards landed, brief hold */
    PASS_SUB_REVEAL     = 5,  /* received cards shown face-up in staging */
    PASS_SUB_RECEIVE    = 6,  /* staged cards animate into hands */
} PassSubphase;

#define DEFAULT_PASS_CARD_COUNT 3
#define MAX_PASS_CARD_COUNT     4
#define PASS_CARD_COUNT         DEFAULT_PASS_CARD_COUNT /* backwards compat */
#define GAME_OVER_SCORE 100

typedef struct GameState {
    GamePhase     phase;
    Player        players[NUM_PLAYERS];
    Deck          deck;

    /* Round tracking */
    int           round_number;   /* completed rounds (incremented at end of new_round) */
    PassDirection pass_direction;  /* set by dealer or default */
    int           pass_card_count; /* 0, 2, 3, or 4 (set by dealer) */
    int           lead_player;    /* who leads current trick */
    bool          hearts_broken;  /* has a heart been played this round */

    /* Trick tracking */
    Trick         current_trick;
    int           tricks_played;  /* 0..12 within a round */

    /* Passing state */
    Card          pass_selections[NUM_PLAYERS][MAX_PASS_CARD_COUNT];
    bool          pass_ready[NUM_PLAYERS];
    bool          skip_human_pass_sort; /* when true, don't sort human hand after pass */
} GameState;

/* Initialize game: set up 4 players (player 0 = human), phase = PHASE_MENU */
void game_state_init(GameState *gs);

/* Start a new game from PHASE_MENU or PHASE_GAME_OVER. Resets scores and
 * round_number, then delegates to game_state_new_round(). Returns false
 * if called from any other phase. */
bool game_state_start_game(GameState *gs);

/* Reset to menu state. Callable from any phase. Delegates to
 * game_state_init() for a full wipe. */
void game_state_reset_to_menu(GameState *gs);

/* Start a new round: shuffle, deal, set pass direction, find 2♣ holder. */
void game_state_new_round(GameState *gs);

/* Find which player holds 2♣. Returns 0-3, or -1 if not found. */
int game_state_find_two_of_clubs(const GameState *gs);

/* Submit pass selection for a player. Returns true on success. */
bool game_state_select_pass(GameState *gs, int player_id,
                            const Card cards[], int card_count);

/* Check if all 4 players have confirmed their pass selections. */
bool game_state_all_passes_ready(const GameState *gs);

/* Execute the pass exchange. Returns false if not all ready. */
bool game_state_execute_pass(GameState *gs);

/* Return player_id (0-3) whose turn it is. Returns -1 if not PHASE_PLAYING
 * or trick is already complete (signals caller to call complete_trick). */
int game_state_current_player(const GameState *gs);

/* Play a card for the given player. Validates phase, turn order, and legality.
 * Removes card from hand, adds to trick, updates hearts_broken.
 * Does NOT auto-complete the trick. Returns true on success. */
bool game_state_play_card(GameState *gs, int player_id, Card card);

/* Complete the current trick. Awards points to winner, increments tricks_played.
 * After 13 tricks: checks shoot-the-moon, calls player_add_to_total() for all,
 * transitions to PHASE_SCORING. Otherwise: inits next trick with winner as lead.
 * Returns false if trick not complete or wrong phase. */
bool game_state_complete_trick(GameState *gs);

/* Same as game_state_complete_trick but with caller-supplied winner and points.
 * Used by Phase 2 when transmutation cards override trick resolution. */
bool game_state_complete_trick_with(GameState *gs, int winner, int points);

/* Pure query: would this card play be legal for this player right now?
 * Checks phase, turn order, and delegates to trick_is_valid_play(). */
bool game_state_is_valid_play(const GameState *gs, int player_id, Card card);

/* Pure query: returns true if any player's total_score >= GAME_OVER_SCORE. */
bool game_state_is_game_over(const GameState *gs);

/* Advance out of PHASE_SCORING: either start a new round or end the game.
 * Returns false if not in PHASE_SCORING. */
bool game_state_advance_scoring(GameState *gs);

/* Write the IDs of the winning player(s) (lowest total_score) into winners[].
 * Returns the count of winners (>1 means a tie). Returns 0 if not PHASE_GAME_OVER. */
int game_state_get_winners(const GameState *gs, int winners[NUM_PLAYERS]);

#endif /* GAME_STATE_H */
