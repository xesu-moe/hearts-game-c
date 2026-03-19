#ifndef AI_H
#define AI_H

/* ============================================================
 * @deps-exports: ai_select_pass(), ai_play_card()
 * @deps-requires: core/game_state.h, phase2/phase2_state.h,
 *                 game/play_phase.h
 * @deps-used-by: game/turn_flow.c, game/pass_phase.c
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "core/game_state.h"
#include "phase2/phase2_state.h"
#include "game/play_phase.h"

/* Forward declarations */
typedef struct RenderState RenderState;

/* AI passes the 3 highest-point cards. */
void ai_select_pass(GameState *gs, int player_id);

/* AI plays the first legal card from hand. */
void ai_play_card(GameState *gs, RenderState *rs, Phase2State *p2,
                  PlayPhaseState *pps, int player_id);

#endif /* AI_H */
