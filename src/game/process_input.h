#ifndef PROCESS_INPUT_H
#define PROCESS_INPUT_H

/* ============================================================
 * @deps-exports: process_input()
 * @deps-requires: core/game_state.h, render/render.h,
 *                 game/pass_phase.h, game/play_phase.h, game/turn_flow.h
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "core/game_state.h"
#include "game/pass_phase.h"
#include "game/play_phase.h"
#include "game/turn_flow.h"

/* Forward declarations */
typedef struct RenderState RenderState;

void process_input(GameState *gs, RenderState *rs,
                   PassPhaseState *pps, PlayPhaseState *pls,
                   Phase2State *p2, FlowStep flow_step);

#endif /* PROCESS_INPUT_H */
