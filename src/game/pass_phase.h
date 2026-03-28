#ifndef PASS_PHASE_H
#define PASS_PHASE_H

/* ============================================================
 * @deps-exports: PassPhaseState, PASS_*_TIME constants,
 *                PASS_REVEAL_DURATION, advance_pass_subphase,
 *                auto_select_human_pass, finalize_card_pass,
 *                pass_start_toss_anim, pass_toss_animations_done,
 *                pass_start_receive_anim, pass_receive_animations_done,
 *                pass_subphase_update, setup_draft_ui,
 *                draft_finish_round,
 *                pass_subphase_time_limit
 * @deps-requires: core/game_state.h (GameState, PassSubphase),
 *                 core/settings.h (GameSettings - forward decl),
 *                 phase2/phase2_state.h (Phase2State, DraftState)
 * @deps-used-by: update.c, process_input.c, main.c
 * @deps-last-changed: 2026-03-23 — Step 10: Added bool online parameter to pass_subphase_update
 * ============================================================ */

#include <stdbool.h>

#include "core/game_state.h"
#include "phase2/phase2_state.h"

/* Forward declarations */
typedef struct RenderState RenderState;
typedef struct GameSettings GameSettings;

#define PASS_DEALER_TIME         30.0f
#define PASS_CONTRACT_TIME       30.0f  /* must match DRAFT_TIMER_SECONDS */
#define PASS_CARD_PASS_TIME      60.0f
#define PASS_REVEAL_DURATION     2.0f   /* show received cards face-up in staging */
#define PASS_AI_DEALER_DISPLAY   1.2f   /* brief delay for AI dealer choice */
#define PASS_DEALER_ANNOUNCE     1.0f   /* show dealer choice message before contracts */

/* Valid dealer amounts */
static const int DEALER_AMOUNTS[] = {0, 2, 3, 4};
#define DEALER_AMOUNT_COUNT 4

typedef struct PassPhaseState {
    PassSubphase subphase;
    float        timer;
    bool         dealer_ui_active;
    bool         ai_dealer_pending;
    bool         dealer_announced;  /* true = showing announcement, waiting */
    float        dealer_announce_timer;
    /* Dealer selection state */
    int          dealer_dir;   /* PassDirection: 0=left, 1=right, 2=across */
    int          dealer_amt;   /* 0, 2, 3, or 4 */
} PassPhaseState;

/* Determine dealer: player with highest prev_round_points.
 * Returns -1 if round_number <= 1. Breaks ties by total score, then lowest id. */
int dealer_determine_player(const int prev_round_points[NUM_PLAYERS],
                            const GameState *gs);

void setup_dealer_ui(PassPhaseState *pps, RenderState *rs, int dealer_player_id);

/* Show dealer choice announcement and push to chat log. */
void dealer_announce(PassPhaseState *pps, RenderState *rs);

void advance_pass_subphase(PassPhaseState *pps, GameState *gs,
                           RenderState *rs, Phase2State *p2,
                           PassSubphase next);

void auto_select_human_pass(GameState *gs, RenderState *rs);

void finalize_card_pass(PassPhaseState *pps, GameState *gs,
                        RenderState *rs, Phase2State *p2);

/* Start toss animation: all players' selected cards fly face-down to staging
 * area in front of the destination player. Replaces finalize_card_pass as the
 * confirm handler entry point. */
void pass_start_toss_anim(PassPhaseState *pps, GameState *gs,
                          RenderState *rs, Phase2State *p2);

/* Check if all toss animations have completed. */
bool pass_toss_animations_done(const RenderState *rs);

/* After reveal timer: execute logical pass, animate cards into recipient hands. */
void pass_start_receive_anim(PassPhaseState *pps, GameState *gs,
                             RenderState *rs, Phase2State *p2,
                             const GameSettings *settings);

/* Check if all receive animations have completed. */
bool pass_receive_animations_done(const RenderState *rs);

void pass_subphase_update(PassPhaseState *pps, GameState *gs,
                          RenderState *rs, Phase2State *p2,
                          const GameSettings *settings, float dt,
                          bool online);

void setup_draft_ui(RenderState *rs, Phase2State *p2);

/* Sync pass phase UI from server state (online mode only).
 * Called after state_recv_apply to populate render state for the current
 * server-driven subphase. */
void pass_sync_online_ui(PassPhaseState *pps, GameState *gs,
                         RenderState *rs, Phase2State *p2);

/* Advance draft to next round or finalize. Called after all players pick. */
void draft_finish_round(PassPhaseState *pps, GameState *gs,
                        RenderState *rs, Phase2State *p2);

float pass_subphase_time_limit(PassSubphase sub);

#endif /* PASS_PHASE_H */
