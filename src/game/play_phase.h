#ifndef PLAY_PHASE_H
#define PLAY_PHASE_H

/* ============================================================
 * @deps-exports: PlayPhaseState (incl. card_played_sfx, transmute_sfx fields),
 *                play_card_with_transmute(), p2_player_name()
 * @deps-requires: core/game_state.h (GameState), core/card.h (Card),
 *                 phase2/phase2_state.h (Phase2State), phase2/transmutation.h
 * @deps-used-by: game/ai.c, game/turn_flow.c, game/update.c, main.c
 * @deps-last-changed: 2026-03-19 — Added card_played_sfx, transmute_sfx flags
 * ============================================================ */

#include <stdbool.h>

#include "core/card.h"
#include "core/game_state.h"
#include "phase2/phase2_state.h"
#include "phase2/transmutation.h"

/* Forward declarations */
typedef struct RenderState RenderState;

typedef struct PlayPhaseState {
    int               pending_transmutation;  /* selected inv slot, -1 = none */
    TrickTransmuteInfo current_tti;           /* parallel to current trick */
    bool              card_played_sfx;        /* set on any successful play */
    bool              transmute_sfx;          /* set on transmuted card play */
} PlayPhaseState;

/* Play a card with transmutation awareness. Returns true on success. */
bool play_card_with_transmute(GameState *gs, RenderState *rs,
                              Phase2State *p2, PlayPhaseState *pps,
                              int player_id, Card card);

/* Get display name for a player ID. */
const char *p2_player_name(int player_id);

#endif /* PLAY_PHASE_H */
