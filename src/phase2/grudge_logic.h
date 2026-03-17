#ifndef GRUDGE_LOGIC_H
#define GRUDGE_LOGIC_H

/* ============================================================
 * @deps-exports: grudge_state_init(), grudge_round_reset(),
 *                grudge_check_trick(), grudge_grant_token(),
 *                grudge_set_token(), grudge_consume_token(),
 *                grudge_get_revenge_options(), grudge_apply_revenge(),
 *                grudge_ai_decide(), MAX_GRUDGE_REVENGE_OPTIONS
 * @deps-requires: phase2_state.h (Phase2State, GrudgeToken),
 *                 core/trick.h (Trick)
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-16 — Initial creation with GrudgeToken struct
 * ============================================================ */

#include <stdbool.h>

#include "phase2_state.h"
#include "core/trick.h"

#define MAX_GRUDGE_REVENGE_OPTIONS 4

/* Zero all grudge tokens across all players. */
void grudge_state_init(Phase2State *p2);

/* Reset used_this_round flags for a new round (tokens persist). */
void grudge_round_reset(Phase2State *p2);

/* Scan a completed trick for the Queen of Spades.
 * If found: sets *out_attacker to the QoS player, *out_victim to the winner.
 * Returns true if a grudge should be granted (attacker != victim).
 * Returns false otherwise. */
bool grudge_check_trick(const Trick *trick, int winner,
                        int *out_attacker, int *out_victim);

/* Grant a grudge token to victim against attacker.
 * Returns 0 if granted successfully, 1 if conflict (player already has token). */
int grudge_grant_token(Phase2State *p2, int victim, int attacker);

/* Force-set a grudge token after discard choice resolves. */
void grudge_set_token(Phase2State *p2, int player, int attacker);

/* Clear a player's grudge token (after use). */
void grudge_consume_token(Phase2State *p2, int player);

/* Collect revenge IDs available to a player from their Queen characters.
 * Writes up to MAX_GRUDGE_REVENGE_OPTIONS ids into out_ids[].
 * Falls back to first N from g_revenge_defs[] if no queens assigned.
 * Returns the number of options written. */
int grudge_get_revenge_options(const Phase2State *p2, int player,
                               int out_ids[MAX_GRUDGE_REVENGE_OPTIONS]);

/* Look up RevengeDef, create ActiveEffect(s) targeting attacker,
 * then consume the token. */
void grudge_apply_revenge(Phase2State *p2, int player, int revenge_id);

/* AI heuristic: if token available, use it with a random revenge.
 * Does nothing if no token or already used this round. */
void grudge_ai_decide(Phase2State *p2, int player);

#endif /* GRUDGE_LOGIC_H */
