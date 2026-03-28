#ifndef STATE_RECV_H
#define STATE_RECV_H

/* ============================================================
 * State Application: Server → Client
 *
 * Applies a NetPlayerView (server-authoritative state snapshot)
 * to the local GameState and Phase2State. Handles seat remapping
 * so that the local human player is always player 0.
 *
 * Does NOT touch game-layer types (PassPhaseState, PlayPhaseState,
 * TurnFlow) — those are applied in main.c after this call.
 *
 * @deps-exports: state_recv_apply(GameState *, Phase2State *, const NetPlayerView *, bool)
 * @deps-requires: core/game_state.h (GameState), phase2/phase2_state.h (Phase2State),
 *                 net/protocol.h (NetPlayerView)
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-28 — Added defer_trick parameter
 * ============================================================ */

#include "core/game_state.h"
#include "phase2/phase2_state.h"
#include "net/protocol.h"

/* Apply a server NetPlayerView to local GameState and Phase2State.
 * Handles seat remapping (server my_seat → local player 0).
 * Does NOT set sync_needed on RenderState — caller must do that.
 *
 * defer_trick: when true, skip overwriting gs->current_trick.
 * Use during trick animations to prevent server state from
 * clobbering the trick data the animation depends on. */
void state_recv_apply(GameState *gs, Phase2State *p2,
                      const NetPlayerView *view, bool defer_trick);

#endif /* STATE_RECV_H */
