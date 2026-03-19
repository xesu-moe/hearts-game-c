#ifndef INFO_SYNC_H
#define INFO_SYNC_H

/* ============================================================
 * @deps-exports: info_sync_update(), info_sync_playability()
 * @deps-requires: core/game_state.h, render/render.h,
 *                 phase2/phase2_state.h, game/play_phase.h
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "core/game_state.h"
#include "phase2/phase2_state.h"
#include "game/play_phase.h"

/* Forward declarations */
typedef struct RenderState RenderState;

/* Sync info panel (contract, vendetta, bonuses, transmutations, vendetta options). */
void info_sync_update(GameState *gs, RenderState *rs, Phase2State *p2,
                      PlayPhaseState *pls);

/* Compute playability flags for human hand (transmute-aware). */
void info_sync_playability(GameState *gs, RenderState *rs, Phase2State *p2);

#endif /* INFO_SYNC_H */
