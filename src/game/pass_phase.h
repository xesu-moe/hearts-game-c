#ifndef PASS_PHASE_H
#define PASS_PHASE_H

/* ============================================================
 * @deps-exports: PassPhaseState, PASS_*_TIME constants,
 *                PASS_REVEAL_DURATION, advance_pass_subphase,
 *                auto_select_human_pass, finalize_card_pass,
 *                pass_start_toss_anim, pass_toss_animations_done,
 *                pass_start_receive_anim, pass_receive_animations_done,
 *                pass_subphase_update, setup_draft_ui,
 *                draft_finish_round, setup_vendetta_ui,
 *                pass_subphase_time_limit
 * @deps-requires: core/game_state.h (GameState, PassSubphase),
 *                 core/settings.h (GameSettings - forward decl),
 *                 phase2/phase2_state.h (Phase2State, DraftState)
 * @deps-used-by: update.c, process_input.c, main.c
 * @deps-last-changed: 2026-03-22 — Added PASS_REVEAL_DURATION, GameSettings params to pass_start_receive_anim/pass_subphase_update
 * ============================================================ */

#include <stdbool.h>

#include "core/game_state.h"
#include "phase2/phase2_state.h"

/* Forward declarations */
typedef struct RenderState RenderState;
typedef struct GameSettings GameSettings;

#define PASS_VENDETTA_TIME       10.0f
#define PASS_CONTRACT_TIME       30.0f  /* must match DRAFT_TIMER_SECONDS */
#define PASS_CARD_PASS_TIME      60.0f
#define PASS_AI_VENDETTA_DISPLAY 1.2f
#define PASS_REVEAL_DURATION     2.0f   /* show received cards face-up in staging */

typedef struct PassPhaseState {
    PassSubphase subphase;
    float        timer;
    bool         ai_vendetta_pending;
    bool         vendetta_ui_active;
} PassPhaseState;

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
                          const GameSettings *settings, float dt);

void setup_draft_ui(RenderState *rs, Phase2State *p2);

/* Advance draft to next round or finalize. Called after all players pick. */
void draft_finish_round(PassPhaseState *pps, GameState *gs,
                        RenderState *rs, Phase2State *p2);

void setup_vendetta_ui(RenderState *rs, Phase2State *p2,
                       int timing_filter);

float pass_subphase_time_limit(PassSubphase sub);

#endif /* PASS_PHASE_H */
