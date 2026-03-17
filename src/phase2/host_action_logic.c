/* ============================================================
 * @deps-implements: host_action_logic.h
 * @deps-requires: host_action_logic.h, phase2_state.h, phase2_defs.h,
 *                 core/game_state.h
 * @deps-last-changed: 2026-03-16 — Initial creation
 * ============================================================ */

#include "host_action_logic.h"

#include <raylib.h>

#include "phase2_defs.h"

void host_action_round_reset(Phase2State *p2)
{
    p2->round.chosen_host_action = -1;
    p2->round.host_action_chosen = false;
}

int host_action_determine_host(const GameState *gs, int round_number)
{
    if (round_number <= 1) return -1;

    int host = 0;
    int highest = gs->players[0].total_score;

    for (int i = 1; i < NUM_PLAYERS; i++) {
        if (gs->players[i].total_score > highest) {
            highest = gs->players[i].total_score;
            host = i;
        }
    }

    return host;
}

int host_action_get_available(const Phase2State *p2, int player_id,
                              int out_ids[MAX_HOST_ACTION_OPTIONS])
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return 0;

    int count = 0;

    /* Check if any jacks are assigned for this player */
    bool has_jacks = false;
    for (int s = 0; s < SUIT_COUNT; s++) {
        if (p2->players[player_id].jack_ids[s] >= 0) {
            has_jacks = true;
            break;
        }
    }

    if (!has_jacks) {
        /* Fallback: offer loaded host action defs directly (up to 4) */
        int max = g_host_action_def_count < MAX_HOST_ACTION_OPTIONS
                      ? g_host_action_def_count
                      : MAX_HOST_ACTION_OPTIONS;
        for (int i = 0; i < max; i++) {
            out_ids[count++] = g_host_action_defs[i].id;
        }
        return count;
    }

    /* Normal path: each Jack contributes 1 host action */
    for (int s = 0; s < SUIT_COUNT && count < MAX_HOST_ACTION_OPTIONS; s++) {
        int jack_id = p2->players[player_id].jack_ids[s];
        if (jack_id < 0) continue;

        const CharacterDef *ch = phase2_get_character(jack_id);
        if (!ch || ch->figure_type != FIGURE_JACK) continue;

        if (ch->mechanics.jack.num_host_actions > 0) {
            int aid = ch->mechanics.jack.host_action_ids[0];
            if (aid >= 0) {
                out_ids[count++] = aid;
            }
        }
    }

    return count;
}

void host_action_select(Phase2State *p2, int host_action_id)
{
    p2->round.chosen_host_action = host_action_id;
    p2->round.host_action_chosen = true;
}

void host_action_apply(Phase2State *p2)
{
    if (p2->round.chosen_host_action < 0) return;
    if (!p2->round.host_action_chosen) return;

    const HostActionDef *def = phase2_get_host_action(p2->round.chosen_host_action);
    if (!def) return;

    for (int i = 0; i < def->num_effects; i++) {
        if (p2->round.num_round_effects >= MAX_ACTIVE_EFFECTS) break;

        ActiveEffect *ae = &p2->round.round_effects[p2->round.num_round_effects++];
        ae->effect = def->effects[i];
        ae->scope = def->scope;
        ae->source_player = p2->round.host_player_id;
        ae->target_player = -1;
        ae->rounds_remaining = 0; /* round-scoped */
        ae->active = true;
    }
}

void host_action_ai_select(Phase2State *p2)
{
    int host = p2->round.host_player_id;
    if (host < 0 || host >= NUM_PLAYERS) return;

    int options[MAX_HOST_ACTION_OPTIONS];
    int count = host_action_get_available(p2, host, options);

    if (count <= 0) return;

    int pick = GetRandomValue(0, count - 1);
    host_action_select(p2, options[pick]);
}
