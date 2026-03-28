#ifndef AI_H
#define AI_H

/* ============================================================
 * @deps-exports: ai_select_pass(), ai_play_card(), ai_rogue_choose(),
 *                ai_duel_choose(), ai_duel_execute()
 * @deps-requires: game_state.h (GameState), phase2_state.h, play_phase.h
 * @deps-used-by: turn_flow.c, pass_phase.c, main.c
 * @deps-last-changed: 2026-03-20 — Added ai_duel_choose() for duel decision
 *                     logic (no state mutation, outputs target and own card)
 * ============================================================ */

#include "core/game_state.h"
#include "phase2/phase2_state.h"
#include "play_phase.h"

/* Forward declarations */
typedef struct RenderState RenderState;

/* AI passes the 3 highest-point cards. */
void ai_select_pass(GameState *gs, int player_id);

/* AI plays the first legal card from hand. */
void ai_play_card(GameState *gs, RenderState *rs, Phase2State *p2,
                  PlayPhaseState *pps, int player_id);

/* AI selects first legal card without playing it. Returns true if found. */
bool ai_select_card(const GameState *gs, const Phase2State *p2,
                    int player_id, Card *out);

/* AI picks an opponent's card to reveal (Rogue effect).
 * winner: the player who won the trick. Sets out_player and out_hand_idx. */
void ai_rogue_choose(const GameState *gs, int winner,
                     int *out_player, int *out_hand_idx);

/* AI decides Duel targets without mutating game state.
 * Returns target player, card index in their hand, and own card index. */
void ai_duel_choose(const GameState *gs, int winner,
                    int *out_target_player, int *out_target_idx,
                    int *out_own_idx);

/* AI executes a Duel card swap: picks random opponent + random cards.
 * Syncs render state and pushes chat message. */
void ai_duel_execute(GameState *gs, Phase2State *p2, RenderState *rs,
                     int winner);

#endif /* AI_H */
