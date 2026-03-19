#ifndef UPDATE_H
#define UPDATE_H

/* ============================================================
 * @deps-exports: game_update()
 * @deps-requires: core/game_state.h, core/settings.h, render/render.h,
 *                 game/pass_phase.h, game/play_phase.h, game/settings_ui.h,
 *                 phase2/phase2_state.h
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include <stdbool.h>

#include "core/game_state.h"
#include "core/settings.h"
#include "game/pass_phase.h"
#include "game/play_phase.h"
#include "game/settings_ui.h"
#include "phase2/phase2_state.h"

/* Forward declarations */
typedef struct RenderState RenderState;

void game_update(GameState *gs, RenderState *rs, Phase2State *p2,
                 PassPhaseState *pps, PlayPhaseState *pls,
                 SettingsUIState *sui, GameSettings *settings,
                 float dt, bool *quit_requested);

#endif /* UPDATE_H */
