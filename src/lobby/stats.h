/* ============================================================
 * @deps-exports: stats_calc_elo_deltas(), ELO_K_FACTOR, ELO_DEFAULT, ELO_MIN, ELO_MAX
 * @deps-requires: (none — pure math, no external dependencies)
 * @deps-used-by: lobby/lobby_net.c
 * @deps-last-changed: 2026-03-26 — Step 21: ELO calculation for match results
 * ============================================================ */

#ifndef STATS_H
#define STATS_H

#define ELO_K_FACTOR 32.0
#define ELO_DEFAULT  1000.0
#define ELO_MIN      100.0
#define ELO_MAX      9999.0

/* Calculate ELO rating deltas for a 4-player match.
 *
 * placements[i]   — 1-4 placement for seat i (1 = best). Set to 0
 *                    to skip a seat (AI/empty).
 * current_elos[i] — current ELO rating for seat i.
 * elo_deltas_out[i] — written with the signed ELO change for seat i.
 *                      Caller adds this to current_elos[i] and clamps. */
void stats_calc_elo_deltas(const int placements[4],
                           const double current_elos[4],
                           double elo_deltas_out[4]);

#endif /* STATS_H */
