#ifndef HOST_ACTION_LOGIC_H
#define HOST_ACTION_LOGIC_H

/* ============================================================
 * @deps-exports: host_action_round_reset(), host_action_determine_host(),
 *                host_action_get_available(), host_action_select(),
 *                host_action_apply(), host_action_ai_select(),
 *                MAX_HOST_ACTION_OPTIONS
 * @deps-requires: phase2_state.h (Phase2State), core/game_state.h (GameState)
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-16 — Initial creation
 * ============================================================ */

#include "phase2_state.h"
#include "core/game_state.h"

#define MAX_HOST_ACTION_OPTIONS 4

/* Reset per-round host action state. Sets chosen_host_action = -1,
 * host_action_chosen = false. Does NOT touch round_effects or host_player_id. */
void host_action_round_reset(Phase2State *p2);

/* Determine which player is the Host for this round.
 * Returns -1 if round_number <= 1 (no host on round 1).
 * Otherwise returns the player with the highest total_score (losing player).
 * Ties: lowest player index wins. */
int host_action_determine_host(const GameState *gs, int round_number);

/* Get available host action options for a player (from their Jacks).
 * Writes up to MAX_HOST_ACTION_OPTIONS IDs into out_ids[].
 * Returns the number of available options.
 * Fallback: when jacks are unassigned, offers loaded host action defs. */
int host_action_get_available(const Phase2State *p2, int player_id,
                              int out_ids[MAX_HOST_ACTION_OPTIONS]);

/* Set the chosen host action for this round. */
void host_action_select(Phase2State *p2, int host_action_id);

/* Apply the chosen host action's effects to round_effects[]. */
void host_action_apply(Phase2State *p2);

/* AI host picks a random available host action and selects it. */
void host_action_ai_select(Phase2State *p2);

#endif /* HOST_ACTION_LOGIC_H */
