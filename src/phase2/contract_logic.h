#ifndef CONTRACT_LOGIC_H
#define CONTRACT_LOGIC_H

/* ============================================================
 * @deps-exports: contract_on_trick_complete() [signature changed],
 *                contract_record_received_cards() [new], contract_evaluate(),
 *                contract_select(), contract_get_available(), MAX_CONTRACT_OPTIONS
 * @deps-requires: phase2_state.h (Phase2State), core/trick.h (Trick),
 *                 transmutation.h (TrickTransmuteInfo)
 * @deps-used-by: contract_logic.c, pass_phase.c, turn_flow.c, update.c,
 *                phase_transitions.c, main.c
 * @deps-last-changed: 2026-03-20 — contract_on_trick_complete sig change, new record_received_cards
 * ============================================================ */

#include <stdbool.h>

#include "phase2_state.h"
#include "core/trick.h"

#define MAX_CONTRACT_OPTIONS 4

/* Zero all Phase2State, set contract_id = -1 for all players. */
void contract_state_init(Phase2State *p2);

/* Reset per-round contract instances for a new round. */
void contract_round_reset(Phase2State *p2);

/* Get available contract options for a player. Writes up to 4 contract def
 * IDs into out_ids[]. Returns the number of available options.
 * Fallback: when kings are unassigned, offers loaded contract defs. */
int contract_get_available(const Phase2State *p2, int player_id,
                           int out_ids[MAX_CONTRACT_OPTIONS]);

/* Set a player's active contract for this round. */
void contract_select(Phase2State *p2, int player_id, int contract_id);

/* Returns true if all 4 players have chosen a contract. */
bool contract_all_chosen(const Phase2State *p2);

/* Update contract trackers after a trick completes.
 * trick_number is 0-based. hearts_broken_before is pre-trick state. */
void contract_on_trick_complete(Phase2State *p2, const Trick *trick, int winner,
                                int trick_number, const TrickTransmuteInfo *tti,
                                bool hearts_broken_before);

/* Record cards received during pass phase for passed-card tracking. */
void contract_record_received_cards(Phase2State *p2, int player_id,
                                    const Card cards[], int count);

/* Evaluate a player's contract condition. Sets completed or failed. */
void contract_evaluate(Phase2State *p2, int player_id);

/* Apply reward for a completed contract: add permanent ActiveEffect,
 * advance King tier. Does nothing if contract was not completed. */
void contract_apply_reward(Phase2State *p2, int player_id);

/* AI picks a random available contract. */
void contract_ai_select(Phase2State *p2, int player_id);

#endif /* CONTRACT_LOGIC_H */
