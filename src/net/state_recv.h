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
 * @deps-exports: state_recv_apply
 * @deps-requires: core/game_state.h (GameState),
 *                 phase2/phase2_state.h (Phase2State),
 *                 net/protocol.h (NetPlayerView)
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-23 — Step 9: Initial creation
 * ============================================================ */

#include "core/game_state.h"
#include "phase2/phase2_state.h"
#include "net/protocol.h"

/* Apply a server NetPlayerView to local GameState and Phase2State.
 * Handles seat remapping (server my_seat → local player 0).
 * Does NOT set sync_needed on RenderState — caller must do that. */
void state_recv_apply(GameState *gs, Phase2State *p2,
                      const NetPlayerView *view);

#endif /* STATE_RECV_H */
