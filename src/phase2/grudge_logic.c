/* ============================================================
 * @deps-implements: grudge_logic.h
 * @deps-requires: grudge_logic.h, phase2_state.h (GrudgeToken, PlayerPhase2),
 *                 phase2_defs.h (phase2_get_character, phase2_get_revenge),
 *                 core/trick.h (Trick), core/card.h (SUIT_SPADES, RANK_Q),
 *                 effect.h (ActiveEffect), character.h (CharacterDef),
 *                 revenge.h (RevengeDef)
 * @deps-last-changed: 2026-03-16 — Initial creation
 * ============================================================ */

#include "grudge_logic.h"

#include <stdlib.h>
#include <string.h>

#include "phase2_defs.h"

/* ---- Init / Reset ---- */

void grudge_state_init(Phase2State *p2)
{
    for (int i = 0; i < NUM_PLAYERS; i++) {
        p2->players[i].grudge_token.active = false;
        p2->players[i].grudge_token.attacker_id = -1;
        p2->players[i].grudge_token.used_this_round = false;
    }
}

void grudge_round_reset(Phase2State *p2)
{
    for (int i = 0; i < NUM_PLAYERS; i++) {
        p2->players[i].grudge_token.used_this_round = false;
    }
}

/* ---- Trick scanning ---- */

bool grudge_check_trick(const Trick *trick, int winner,
                        int *out_attacker, int *out_victim)
{
    if (winner < 0 || winner >= NUM_PLAYERS) return false;

    for (int i = 0; i < trick->num_played; i++) {
        if (trick->cards[i].suit == SUIT_SPADES &&
            trick->cards[i].rank == RANK_Q) {
            int attacker = trick->player_ids[i];
            if (attacker == winner) return false; /* self-inflicted */
            *out_attacker = attacker;
            *out_victim = winner;
            return true;
        }
    }
    return false;
}

/* ---- Token management ---- */

int grudge_grant_token(Phase2State *p2, int victim, int attacker)
{
    GrudgeToken *gt = &p2->players[victim].grudge_token;
    if (gt->active) {
        return 1; /* conflict — caller must resolve */
    }
    gt->active = true;
    gt->attacker_id = attacker;
    gt->used_this_round = false;
    return 0;
}

void grudge_set_token(Phase2State *p2, int player, int attacker)
{
    GrudgeToken *gt = &p2->players[player].grudge_token;
    gt->active = true;
    gt->attacker_id = attacker;
    gt->used_this_round = false;
}

void grudge_consume_token(Phase2State *p2, int player)
{
    GrudgeToken *gt = &p2->players[player].grudge_token;
    gt->active = false;
    gt->attacker_id = -1;
    gt->used_this_round = false;
}

/* ---- Revenge options ---- */

int grudge_get_revenge_options(const Phase2State *p2, int player,
                               int out_ids[MAX_GRUDGE_REVENGE_OPTIONS])
{
    int count = 0;
    bool seen[MAX_REVENGE_DEFS] = {false};

    /* Collect from player's Queen characters */
    for (int s = 0; s < SUIT_COUNT && count < MAX_GRUDGE_REVENGE_OPTIONS; s++) {
        int qid = p2->players[player].queen_ids[s];
        if (qid < 0) continue;
        const CharacterDef *ch = phase2_get_character(qid);
        if (!ch || ch->figure_type != FIGURE_QUEEN) continue;

        for (int r = 0; r < ch->mechanics.queen.num_revenges &&
                         count < MAX_GRUDGE_REVENGE_OPTIONS; r++) {
            int rid = ch->mechanics.queen.revenge_ids[r];
            if (rid >= 0 && rid < MAX_REVENGE_DEFS && !seen[rid]) {
                seen[rid] = true;
                out_ids[count++] = rid;
            }
        }
    }

    /* Fallback: first N from global revenge defs */
    if (count == 0) {
        for (int i = 0; i < g_revenge_def_count &&
                         count < MAX_GRUDGE_REVENGE_OPTIONS; i++) {
            out_ids[count++] = g_revenge_defs[i].id;
        }
    }

    return count;
}

/* ---- Apply revenge ---- */

void grudge_apply_revenge(Phase2State *p2, int player, int revenge_id)
{
    const RevengeDef *rd = phase2_get_revenge(revenge_id);
    if (!rd) return;

    int attacker = p2->players[player].grudge_token.attacker_id;
    RoundPhase2 *round = &p2->round;

    for (int i = 0; i < rd->num_effects; i++) {
        if (round->num_round_effects >= MAX_ACTIVE_EFFECTS) break;

        ActiveEffect *ae = &round->round_effects[round->num_round_effects++];
        ae->effect = rd->effects[i];
        ae->scope = rd->scope;
        ae->source_player = player;
        ae->target_player = (rd->scope == EFFECT_SCOPE_TARGET) ? attacker : -1;
        ae->rounds_remaining = 0; /* round-scoped, expire at round end */
        ae->active = true;
    }

    grudge_consume_token(p2, player);
}

/* ---- AI decision ---- */

void grudge_ai_decide(Phase2State *p2, int player)
{
    GrudgeToken *gt = &p2->players[player].grudge_token;
    if (!gt->active || gt->used_this_round) return;

    int options[MAX_GRUDGE_REVENGE_OPTIONS];
    int count = grudge_get_revenge_options(p2, player, options);

    if (count > 0) {
        int pick = rand() % count;
        grudge_apply_revenge(p2, player, options[pick]);
    } else {
        gt->used_this_round = true; /* no options, skip */
    }
}
