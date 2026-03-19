#ifndef VENDETTA_LOGIC_H
#define VENDETTA_LOGIC_H

/* ============================================================
 * @deps-exports: vendetta_round_reset(), vendetta_determine_player(),
 *                vendetta_get_available(), vendetta_select(),
 *                vendetta_apply(), vendetta_ai_activate(),
 *                vendetta_has_options(), MAX_VENDETTA_OPTIONS
 * @deps-requires: phase2_state.h (Phase2State), core/card.h (NUM_PLAYERS),
 *                 vendetta.h (VendettaTiming)
 * @deps-used-by: vendetta_logic.c, pass_phase.c, turn_flow.c, update.c,
 *                info_sync.c, phase_transitions.c, main.c
 * @deps-last-changed: 2026-03-19 — Extended used_by: game modules (pass, turn, update, info, phase)
 * ============================================================ */

#include <stdbool.h>

#include "phase2_state.h"
#include "vendetta.h"

#define MAX_VENDETTA_OPTIONS 4

/* Reset per-round vendetta state. Sets chosen_vendetta = -1,
 * vendetta_used = false, vendetta_chosen = false. */
void vendetta_round_reset(Phase2State *p2);

/* Determine which player gets the vendetta action this round.
 * Returns -1 if round_number <= 1 or all tied at 0.
 * Otherwise returns the player with the highest prev_round_points. */
int vendetta_determine_player(const int prev_round_points[NUM_PLAYERS],
                              int round_number);

/* Get available vendetta options for a player (from their Queens).
 * Filters by timing. Writes up to MAX_VENDETTA_OPTIONS IDs into out_ids[].
 * Returns the number of available options.
 * Fallback: when queens are unassigned, offers loaded vendetta defs. */
int vendetta_get_available(const Phase2State *p2, int player_id,
                           int timing_filter, int out_ids[MAX_VENDETTA_OPTIONS]);

/* Set the chosen vendetta for this round. */
void vendetta_select(Phase2State *p2, int vendetta_id);

/* Apply the chosen vendetta's effects to round_effects[]. */
void vendetta_apply(Phase2State *p2);

/* AI vendetta player activates: picks a random available vendetta
 * matching the given timing filter and applies it. */
void vendetta_ai_activate(Phase2State *p2, int timing_filter);

/* Check if the vendetta player has options for the given timing. */
bool vendetta_has_options(const Phase2State *p2, int timing_filter);

#endif /* VENDETTA_LOGIC_H */
