#ifndef CONTRACT_LOGIC_H
#define CONTRACT_LOGIC_H

/* ============================================================
 * @deps-exports: draft_generate_pool, draft_pick, draft_auto_pick,
 *                draft_ai_pick, draft_all_picked, draft_advance_round,
 *                draft_finalize, draft_is_complete, contract_state_init,
 *                contract_round_reset, contract_all_chosen, contract_on_trick_complete,
 *                contract_record_received_cards, contract_evaluate_all, contract_apply_rewards_all
 * @deps-requires: core/game_state.h (GameState), core/trick.h (Trick),
 *                 phase2_state.h (Phase2State), transmutation.h (TrickTransmuteInfo)
 * @deps-used-by: turn_flow.c, update.c, server_game.c
 * @deps-last-changed: 2026-03-30 — Added const GameState *gs to contract_on_trick_complete() and contract_evaluate_all()
 * ============================================================ */

#include <stdbool.h>

#include "phase2_state.h"
#include "core/trick.h"
#include "core/game_state.h"

/* ---- Draft constants ---- */

#define DRAFT_POOL_SIZE       16
#define DRAFT_ROUNDS           3
#define DRAFT_TIMER_SECONDS   15.0f

/* ---- Draft functions ---- */

/* Generate 16 contract-transmutation pairs, split into 4 groups of 4.
 * Contracts weighted by tier: 60% easy, 30% medium, 10% hard. */
void draft_generate_pool(DraftState *draft);

/* Player picks a pair by index (0..available_count-1). */
void draft_pick(DraftState *draft, int player_id, int pair_index);

/* Auto-pick first available pair (timeout fallback). */
void draft_auto_pick(DraftState *draft, int player_id);

/* AI picks a random pair from available. */
void draft_ai_pick(DraftState *draft, int player_id);

/* Check if all players have picked in the current draft round. */
bool draft_all_picked(const DraftState *draft);

/* Rotate remaining pairs clockwise and advance to next round. */
void draft_advance_round(DraftState *draft);

/* Copy picked pairs into PlayerPhase2.contracts[]. */
void draft_finalize(DraftState *draft, Phase2State *p2);

/* Check if draft is complete (3 rounds done). */
bool draft_is_complete(const DraftState *draft);

/* ---- Contract state management ---- */

void contract_state_init(Phase2State *p2);
void contract_round_reset(Phase2State *p2);
bool contract_all_chosen(const Phase2State *p2);

/* ---- Contract tracking ---- */

void contract_on_trick_complete(Phase2State *p2, const GameState *gs,
                                const Trick *trick, int winner,
                                int trick_number, const TrickTransmuteInfo *tti,
                                bool hearts_broken_before);

void contract_record_received_cards(Phase2State *p2, int player_id,
                                    const Card cards[], int count);

/* ---- Contract evaluation ---- */

void contract_evaluate_all(Phase2State *p2, const GameState *gs, int player_id);
void contract_apply_rewards_all(Phase2State *p2, int player_id);

#endif /* CONTRACT_LOGIC_H */
