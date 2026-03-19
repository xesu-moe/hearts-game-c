/* ============================================================
 * @deps-implements: vendetta_logic.h
 * @deps-requires: vendetta_logic.h, phase2_state.h, phase2_defs.h,
 *                 vendetta.h, character.h
 * @deps-last-changed: 2026-03-18 — Initial creation (merged host_action + revenge)
 * ============================================================ */

#include "vendetta_logic.h"

#include <raylib.h>

#include "phase2_defs.h"

void vendetta_round_reset(Phase2State *p2)
{
    p2->round.chosen_vendetta = -1;
    p2->round.vendetta_used = false;
    p2->round.vendetta_chosen = false;
}

int vendetta_determine_player(const int prev_round_points[NUM_PLAYERS],
                              int round_number)
{
    if (round_number <= 1) return -1;

    int highest = prev_round_points[0];
    int player = 0;
    bool tied = false;

    for (int i = 1; i < NUM_PLAYERS; i++) {
        if (prev_round_points[i] > highest) {
            highest = prev_round_points[i];
            player = i;
            tied = false;
        } else if (prev_round_points[i] == highest) {
            tied = true;
        }
    }

    /* No vendetta if all scored 0 or if top score is tied */
    if (highest == 0 || tied) return -1;
    return player;
}

int vendetta_get_available(const Phase2State *p2, int player_id,
                           int timing_filter,
                           int out_ids[MAX_VENDETTA_OPTIONS])
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return 0;

    int count = 0;

    /* Check if any queens are assigned for this player */
    bool has_queens = false;
    for (int s = 0; s < SUIT_COUNT; s++) {
        if (p2->players[player_id].queen_ids[s] >= 0) {
            has_queens = true;
            break;
        }
    }

    if (!has_queens) {
        /* Fallback: offer loaded vendetta defs matching timing */
        for (int i = 0; i < g_vendetta_def_count &&
                         count < MAX_VENDETTA_OPTIONS; i++) {
            if ((int)g_vendetta_defs[i].timing == timing_filter) {
                out_ids[count++] = g_vendetta_defs[i].id;
            }
        }
        return count;
    }

    /* Normal path: collect from player's Queen characters */
    bool seen[MAX_VENDETTA_DEFS] = {false};
    for (int s = 0; s < SUIT_COUNT && count < MAX_VENDETTA_OPTIONS; s++) {
        int qid = p2->players[player_id].queen_ids[s];
        if (qid < 0) continue;

        const CharacterDef *ch = phase2_get_character(qid);
        if (!ch || ch->figure_type != FIGURE_QUEEN) continue;

        for (int v = 0; v < ch->mechanics.queen.num_vendettas &&
                         count < MAX_VENDETTA_OPTIONS; v++) {
            int vid = ch->mechanics.queen.vendetta_ids[v];
            if (vid < 0 || vid >= MAX_VENDETTA_DEFS || seen[vid]) continue;

            const VendettaDef *vd = phase2_get_vendetta(vid);
            if (!vd || (int)vd->timing != timing_filter) continue;

            seen[vid] = true;
            out_ids[count++] = vid;
        }
    }

    return count;
}

void vendetta_select(Phase2State *p2, int vendetta_id)
{
    p2->round.chosen_vendetta = vendetta_id;
    p2->round.vendetta_chosen = true;
}

void vendetta_apply(Phase2State *p2)
{
    if (p2->round.chosen_vendetta < 0) return;
    if (!p2->round.vendetta_chosen) return;

    const VendettaDef *def = phase2_get_vendetta(p2->round.chosen_vendetta);
    if (!def) return;

    for (int i = 0; i < def->num_effects; i++) {
        if (p2->round.num_round_effects >= MAX_ACTIVE_EFFECTS) break;

        ActiveEffect *ae = &p2->round.round_effects[p2->round.num_round_effects++];
        ae->effect = def->effects[i];
        ae->scope = def->scope;
        ae->source_player = p2->round.vendetta_player_id;
        ae->target_player = -1;
        ae->rounds_remaining = 0; /* round-scoped */
        ae->active = true;
    }

    p2->round.vendetta_used = true;
}

void vendetta_ai_activate(Phase2State *p2, int timing_filter)
{
    int player = p2->round.vendetta_player_id;
    if (player < 0 || player >= NUM_PLAYERS) return;
    if (p2->round.vendetta_used) return;

    int options[MAX_VENDETTA_OPTIONS];
    int count = vendetta_get_available(p2, player, timing_filter, options);

    if (count <= 0) return;

    int pick = GetRandomValue(0, count - 1);
    vendetta_select(p2, options[pick]);
    vendetta_apply(p2);
}

bool vendetta_has_options(const Phase2State *p2, int timing_filter)
{
    int player = p2->round.vendetta_player_id;
    if (player < 0 || player >= NUM_PLAYERS) return false;
    if (p2->round.vendetta_used) return false;

    int options[MAX_VENDETTA_OPTIONS];
    int count = vendetta_get_available(p2, player, timing_filter, options);
    return count > 0;
}
