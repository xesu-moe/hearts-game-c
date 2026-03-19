#ifndef TURN_FLOW_H
#define TURN_FLOW_H

/* ============================================================
 * @deps-exports: FlowStep, TurnFlow, FLOW_* timing constants,
 *                flow_init(), flow_update()
 * @deps-requires: core/game_state.h, core/settings.h
 * @deps-used-by: main.c, game/process_input.c
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "core/game_state.h"
#include "core/settings.h"
#include "phase2/phase2_state.h"
#include "game/play_phase.h"

/* Forward declarations */
typedef struct RenderState RenderState;

typedef enum FlowStep {
    FLOW_IDLE,
    FLOW_WAITING_FOR_HUMAN,
    FLOW_AI_THINKING,
    FLOW_CARD_ANIMATING,
    FLOW_TRICK_DISPLAY,
    FLOW_TRICK_COLLECTING,
    FLOW_BETWEEN_TRICKS,
} FlowStep;

typedef struct TurnFlow {
    FlowStep step;
    float    timer;
    float    turn_timer;
    int      animating_player;
    int      prev_trick_count;
} TurnFlow;

#define FLOW_TURN_TIME_LIMIT   30.0f
#define FLOW_AI_THINK_TIME     0.4f
#define FLOW_CARD_ANIM_TIME    0.25f
#define FLOW_TRICK_DISPLAY_TIME 1.0f
#define FLOW_TRICK_COLLECT_TIME 0.3f
#define FLOW_BETWEEN_TRICKS_TIME 0.2f

void flow_init(TurnFlow *flow);

void flow_update(TurnFlow *flow, GameState *gs, RenderState *rs,
                 Phase2State *p2, GameSettings *settings,
                 PlayPhaseState *pps, float dt);

#endif /* TURN_FLOW_H */
