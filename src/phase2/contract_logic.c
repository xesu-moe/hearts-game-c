/* ============================================================
 * @deps-implements: contract_logic.h
 * @deps-requires: contract_logic.h, phase2_state.h, phase2_defs.h,
 *                 transmutation_logic.h, core/trick.h, core/card.h
 * @deps-last-changed: 2026-03-18 — Added transmutation reward and inventory init
 * ============================================================ */

#include "contract_logic.h"

#include <string.h>

#include <raylib.h>

#include "phase2_defs.h"
#include "transmutation_logic.h"
#include "core/card.h"

void contract_state_init(Phase2State *p2)
{
    memset(p2, 0, sizeof(*p2));
    p2->enabled = false;

    for (int i = 0; i < NUM_PLAYERS; i++) {
        p2->players[i].contract.contract_id = -1;
        p2->players[i].num_persistent = 0;

        /* Mark kings as unassigned (-1) */
        for (int s = 0; s < SUIT_COUNT; s++) {
            p2->players[i].king_ids[s] = -1;
            p2->players[i].queen_ids[s] = -1;
            p2->players[i].jack_ids[s] = -1;
        }

        /* Transmutation system */
        transmute_inv_init(&p2->players[i].transmute_inv);
        transmute_hand_init(&p2->players[i].hand_transmutes);
    }

    p2->round.vendetta_player_id = -1;
    p2->round.chosen_vendetta = -1;
    p2->round.vendetta_used = false;
    p2->round.vendetta_chosen = false;
    p2->round.contracts_chosen = false;
}

void contract_round_reset(Phase2State *p2)
{
    for (int i = 0; i < NUM_PLAYERS; i++) {
        ContractInstance *ci = &p2->players[i].contract;
        ci->contract_id = -1;
        ci->revealed = false;
        ci->completed = false;
        ci->failed = false;
        memset(ci->cards_collected, 0, sizeof(ci->cards_collected));
        ci->tricks_won = 0;
        ci->points_taken = 0;
        ci->has_card = false;

        /* Reset hand transmute state (inventory persists) */
        transmute_hand_init(&p2->players[i].hand_transmutes);
    }

    p2->round.contracts_chosen = false;
    p2->round.num_round_effects = 0;
}

int contract_get_available(const Phase2State *p2, int player_id,
                           int out_ids[MAX_CONTRACT_OPTIONS])
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return 0;

    int count = 0;

    /* Check if any kings are assigned for this player */
    bool has_kings = false;
    for (int s = 0; s < SUIT_COUNT; s++) {
        if (p2->players[player_id].king_ids[s] >= 0) {
            has_kings = true;
            break;
        }
    }

    if (!has_kings) {
        /* Fallback: offer loaded contract defs directly (up to 4) */
        int max = g_contract_def_count < MAX_CONTRACT_OPTIONS
                      ? g_contract_def_count
                      : MAX_CONTRACT_OPTIONS;
        for (int i = 0; i < max; i++) {
            out_ids[count++] = g_contract_defs[i].id;
        }
        return count;
    }

    /* Normal path: offer contracts from player's Kings at current tier */
    for (int s = 0; s < SUIT_COUNT && count < MAX_CONTRACT_OPTIONS; s++) {
        int king_id = p2->players[player_id].king_ids[s];
        if (king_id < 0) continue;

        int tier = p2->players[player_id].king_progress[s].current_tier;
        if (tier >= CONTRACT_TIERS) continue; /* exhausted */

        /* Find the character's contract for this tier */
        const CharacterDef *ch = phase2_get_character(king_id);
        if (!ch || ch->figure_type != FIGURE_KING) continue;

        int cid = ch->mechanics.king.contract_ids[tier];
        if (cid >= 0) {
            out_ids[count++] = cid;
        }
    }

    return count;
}

void contract_select(Phase2State *p2, int player_id, int contract_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    p2->players[player_id].contract.contract_id = contract_id;
}

bool contract_all_chosen(const Phase2State *p2)
{
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (p2->players[i].contract.contract_id < 0) {
            return false;
        }
    }
    return true;
}

void contract_on_trick_complete(Phase2State *p2, const Trick *trick, int winner)
{
    if (winner < 0 || winner >= NUM_PLAYERS) return;

    /* The winner collects the cards */
    ContractInstance *wi = &p2->players[winner].contract;
    if (wi->contract_id < 0) return; /* no active contract */

    wi->tricks_won++;

    for (int i = 0; i < trick->num_played; i++) {
        Card c = trick->cards[i];
        int pts = card_points(c);

        wi->cards_collected[c.suit]++;
        wi->points_taken += pts;

        /* Check specific card tracking */
        const ContractDef *cd = phase2_get_contract(wi->contract_id);
        if (cd) {
            if ((cd->condition == COND_COLLECT_CARD || cd->condition == COND_AVOID_CARD) &&
                card_equals(c, cd->cond_param.card)) {
                wi->has_card = true;
            }
        }
    }

}

void contract_evaluate(Phase2State *p2, int player_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    ContractInstance *ci = &p2->players[player_id].contract;
    if (ci->contract_id < 0) return;

    const ContractDef *cd = phase2_get_contract(ci->contract_id);
    if (!cd) {
        ci->failed = true;
        return;
    }

    bool success = false;

    switch (cd->condition) {
    case COND_AVOID_SUIT:
        success = (ci->cards_collected[cd->cond_param.suit] == 0);
        break;

    case COND_COLLECT_N_OF_SUIT:
        if (cd->cond_param.at_least) {
            success = (ci->cards_collected[cd->cond_param.suit] >= cd->cond_param.count);
        } else {
            success = (ci->cards_collected[cd->cond_param.suit] == cd->cond_param.count);
        }
        break;

    case COND_WIN_N_TRICKS:
        if (cd->cond_param.at_least) {
            success = (ci->tricks_won >= cd->cond_param.count);
        } else {
            success = (ci->tricks_won == cd->cond_param.count);
        }
        break;

    case COND_TAKE_NO_POINTS:
        success = (ci->points_taken == 0);
        break;

    case COND_TAKE_EXACT_POINTS:
        success = (ci->points_taken == cd->cond_param.count);
        break;

    case COND_AVOID_CARD:
        success = !ci->has_card;
        break;

    case COND_COLLECT_CARD:
        success = ci->has_card;
        break;

    case COND_WIN_LAST_TRICK:
        /* This would need trick number tracking; approximate with tricks_won > 0
         * and external last-trick-winner check. For now, we rely on the caller
         * setting has_card or a dedicated flag in future. Mark as failed. */
        success = false;
        break;

    case COND_NONE:
    case COND_TYPE_COUNT:
    default:
        success = false;
        break;
    }

    if (success) {
        ci->completed = true;
    } else {
        ci->failed = true;
    }
}

void contract_apply_reward(Phase2State *p2, int player_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    ContractInstance *ci = &p2->players[player_id].contract;
    if (!ci->completed) return;

    const ContractDef *cd = phase2_get_contract(ci->contract_id);
    if (!cd) return;

    PlayerPhase2 *pp = &p2->players[player_id];

    /* Add permanent effects from rewards */
    for (int r = 0; r < cd->num_rewards; r++) {
        if (pp->num_persistent >= MAX_ACTIVE_EFFECTS) break;

        ActiveEffect *ae = &pp->persistent_effects[pp->num_persistent++];
        ae->effect = cd->rewards[r];
        ae->scope = cd->reward_scope;
        ae->source_player = player_id;
        ae->target_player = -1;
        ae->rounds_remaining = -1; /* permanent */
        ae->active = true;
    }

    /* Grant transmutation card rewards */
    for (int r = 0; r < cd->num_transmute_rewards; r++) {
        if (cd->transmute_reward_ids[r] >= 0) {
            transmute_inv_add(&pp->transmute_inv, cd->transmute_reward_ids[r]);
        }
    }

    /* Advance King tier for the suit whose King provided this contract.
     * With fallback mode (unassigned kings), skip tier advancement. */
    for (int s = 0; s < SUIT_COUNT; s++) {
        if (pp->king_ids[s] < 0) continue;

        const CharacterDef *ch = phase2_get_character(pp->king_ids[s]);
        if (!ch || ch->figure_type != FIGURE_KING) continue;

        for (int t = 0; t < CONTRACT_TIERS; t++) {
            if (ch->mechanics.king.contract_ids[t] == ci->contract_id) {
                KingProgress *kp = &pp->king_progress[s];
                if (kp->current_tier < CONTRACT_TIERS) {
                    kp->tier_completed[kp->current_tier] = true;
                    kp->current_tier++;
                }
                return;
            }
        }
    }
}

void contract_ai_select(Phase2State *p2, int player_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;

    int options[MAX_CONTRACT_OPTIONS];
    int count = contract_get_available(p2, player_id, options);

    if (count <= 0) return;

    int pick = GetRandomValue(0, count - 1);
    contract_select(p2, player_id, options[pick]);
}
