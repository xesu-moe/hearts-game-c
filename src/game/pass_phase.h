#ifndef PASS_PHASE_H
#define PASS_PHASE_H

/* ============================================================
 * @deps-exports: PassPhaseState, PASS_*_TIME constants,
 *                advance_pass_subphase(), auto_select_human_pass(),
 *                finalize_card_pass(), pass_start_toss_anim(),
 *                pass_toss_animations_done(), pass_start_receive_anim(),
 *                pass_receive_animations_done(), pass_subphase_update(),
 *                setup_contract_ui(), setup_vendetta_ui()
 * @deps-requires: core/game_state.h, render/render.h,
 *                 phase2/phase2_state.h
 * @deps-used-by: main.c, game/update.c, game/process_input.c
 * @deps-last-changed: 2026-03-19 — Added pass toss/receive animation functions
 * ============================================================ */

#include <stdbool.h>

#include "core/game_state.h"
#include "phase2/phase2_state.h"

/* Forward declarations */
typedef struct RenderState RenderState;

#define PASS_VENDETTA_TIME       10.0f
#define PASS_CONTRACT_TIME       60.0f
#define PASS_CARD_PASS_TIME      60.0f
#define PASS_AI_VENDETTA_DISPLAY 1.2f

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

/* After wait timer: execute logical pass, animate cards into recipient hands. */
void pass_start_receive_anim(PassPhaseState *pps, GameState *gs,
                             RenderState *rs, Phase2State *p2);

/* Check if all receive animations have completed. */
bool pass_receive_animations_done(const RenderState *rs);

void pass_subphase_update(PassPhaseState *pps, GameState *gs,
                          RenderState *rs, Phase2State *p2, float dt);

void setup_contract_ui(RenderState *rs, Phase2State *p2);

void setup_vendetta_ui(RenderState *rs, Phase2State *p2,
                       int timing_filter);

float pass_subphase_time_limit(PassSubphase sub);

#endif /* PASS_PHASE_H */
