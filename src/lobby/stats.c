/* ============================================================
 * @deps-implements: lobby/stats.h
 * @deps-requires: lobby/stats.h (ELO_K_FACTOR), math.h (pow)
 * @deps-last-changed: 2026-03-26 — Step 21: Placement-based ELO calculation
 * ============================================================ */

#include "stats.h"

#include <math.h>

void stats_calc_elo_deltas(const int placements[4],
                           const double current_elos[4],
                           double elo_deltas_out[4])
{
    for (int i = 0; i < 4; i++)
        elo_deltas_out[i] = 0.0;

    /* For each pair (i, j) where player i placed better than j,
     * treat it as a virtual 1v1 match: i won, j lost. */
    for (int i = 0; i < 4; i++) {
        if (placements[i] < 1) continue; /* skip empty/AI seats */
        for (int j = i + 1; j < 4; j++) {
            if (placements[j] < 1) continue;

            double expected_i =
                1.0 / (1.0 + pow(10.0, (current_elos[j] - current_elos[i]) / 400.0));

            double actual_i, actual_j;
            if (placements[i] < placements[j]) {
                actual_i = 1.0; /* i won */
                actual_j = 0.0;
            } else if (placements[i] > placements[j]) {
                actual_i = 0.0;
                actual_j = 1.0; /* j won */
            } else {
                actual_i = 0.5; /* tie */
                actual_j = 0.5;
            }

            elo_deltas_out[i] += ELO_K_FACTOR * (actual_i - expected_i);
            elo_deltas_out[j] += ELO_K_FACTOR * (actual_j - (1.0 - expected_i));
        }
    }
}
