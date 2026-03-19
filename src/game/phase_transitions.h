#ifndef PHASE_TRANSITIONS_H
#define PHASE_TRANSITIONS_H

/* ============================================================
 * @deps-exports: phase_transition_update(), phase_transition_post_render()
 * @deps-requires: core/game_state.h, render/render.h,
 *                 game/pass_phase.h, game/play_phase.h, game/turn_flow.h,
 *                 phase2/phase2_state.h
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include <stdbool.h>

#include "core/game_state.h"
#include "game/pass_phase.h"
#include "game/play_phase.h"
#include "game/turn_flow.h"
#include "phase2/phase2_state.h"

/* Forward declarations */
typedef struct RenderState RenderState;

/* Handle phase entry/exit logic and chat log entries.
 * Call BEFORE render_update(). Updates prev_phase. */
void phase_transition_update(GameState *gs, RenderState *rs,
                             Phase2State *p2, PassPhaseState *pps,
                             PlayPhaseState *pls, TurnFlow *flow,
                             GamePhase *prev_phase,
                             bool *prev_hearts_broken);

/* Hearts-broken particle burst. Call AFTER render_update().
 * Updates prev_hearts_broken. */
void phase_transition_post_render(GameState *gs, RenderState *rs,
                                  bool *prev_hearts_broken);

#endif /* PHASE_TRANSITIONS_H */
