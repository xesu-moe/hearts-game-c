/* ============================================================
 * @deps-implements: contract_logic.h
 * @deps-requires: contract_logic.h, phase2_state.h, phase2_defs.h,
 *                 transmutation_logic.h (transmute_round_state_init),
 *                 core/trick.h, core/card.h, raylib.h, string.h
 * @deps-last-changed: 2026-03-20 — Mirror: init global history fields in contract_state_init
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

    /* Mirror history (game-scoped) */
    p2->last_played_transmute_id = -1;
    p2->last_played_resolved_effect = TEFFECT_NONE;
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

        /* New tracking fields */
        ci->tricks_won_mask = 0;
        ci->current_streak = 0;
        ci->max_streak = 0;
        memset(ci->led_with_suit, 0, sizeof(ci->led_with_suit));
        ci->broke_hearts = false;
        ci->led_qs_trick = false;
        ci->played_card_first_of_suit = false;
        for (int j = 0; j < PASS_CARD_COUNT; j++)
            ci->received_in_pass[j] = CARD_NONE;
        ci->num_received = 0;
        ci->won_with_passed_card = false;
        ci->hit_with_passed_card = false;
        memset(ci->hits_dealt, 0, sizeof(ci->hits_dealt));
        ci->prevented_moon = false;
        ci->hit_with_transmute = false;

        /* Reset hand transmute state (inventory persists) */
        transmute_hand_init(&p2->players[i].hand_transmutes);
    }

    p2->round.contracts_chosen = false;
    p2->round.num_round_effects = 0;
    memset(p2->round.suit_seen, 0, sizeof(p2->round.suit_seen));
    transmute_round_state_init(&p2->round.transmute_round);
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

void contract_record_received_cards(Phase2State *p2, int player_id,
                                    const Card cards[], int count)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    ContractInstance *ci = &p2->players[player_id].contract;
    int n = count < PASS_CARD_COUNT ? count : PASS_CARD_COUNT;
    for (int i = 0; i < n; i++)
        ci->received_in_pass[i] = cards[i];
    ci->num_received = n;
}

/* Helper: check if card is in a player's received_in_pass list */
static bool is_received_card(const ContractInstance *ci, Card c)
{
    for (int i = 0; i < ci->num_received; i++) {
        if (card_equals(ci->received_in_pass[i], c))
            return true;
    }
    return false;
}

/* Queen of Spades constant */
static const Card QOS = { .suit = SUIT_SPADES, .rank = RANK_Q };

void contract_on_trick_complete(Phase2State *p2, const Trick *trick, int winner,
                                int trick_number, const TrickTransmuteInfo *tti,
                                bool hearts_broken_before)
{
    if (winner < 0 || winner >= NUM_PLAYERS) return;
    if (trick_number < 0 || trick_number > 12) return;

    /* --- Section 1: Pre-compute --- */
    int trick_scoring_pts = 0;
    bool qs_in_trick = false;
    for (int i = 0; i < trick->num_played; i++) {
        trick_scoring_pts += card_points(trick->cards[i]);
        if (card_equals(trick->cards[i], QOS))
            qs_in_trick = true;
    }

    /* Map: which play-order index does each player own */
    int player_play_idx[NUM_PLAYERS];
    for (int p = 0; p < NUM_PLAYERS; p++)
        player_play_idx[p] = -1;
    for (int i = 0; i < trick->num_played; i++)
        player_play_idx[trick->player_ids[i]] = i;

    /* --- Section 2: PREVENT_MOON (before updating winner's points) --- */
    if (trick_scoring_pts > 0) {
        int total_distributed = 0;
        for (int p = 0; p < NUM_PLAYERS; p++)
            total_distributed += p2->players[p].contract.points_taken;

        for (int p = 0; p < NUM_PLAYERS; p++) {
            if (p == winner) continue;
            int pts = p2->players[p].contract.points_taken;
            if (pts > 0 && pts == total_distributed) {
                /* Winner is breaking P's moon run */
                p2->players[winner].contract.prevented_moon = true;
                break;
            }
        }
    }

    /* --- Section 3: Winner tracking --- */
    ContractInstance *wi = &p2->players[winner].contract;
    wi->tricks_won++;
    wi->tricks_won_mask |= (uint16_t)(1 << trick_number);
    wi->current_streak++;
    if (wi->current_streak > wi->max_streak)
        wi->max_streak = wi->current_streak;

    /* Winner card collection */
    for (int i = 0; i < trick->num_played; i++) {
        Card c = trick->cards[i];
        wi->cards_collected[c.suit]++;
        wi->points_taken += card_points(c);

        /* Specific card tracking (COLLECT_CARD / AVOID_CARD) */
        if (wi->contract_id >= 0) {
            const ContractDef *cd = phase2_get_contract(wi->contract_id);
            if (cd && (cd->condition == COND_COLLECT_CARD ||
                       cd->condition == COND_AVOID_CARD) &&
                card_equals(c, cd->cond_param.card)) {
                wi->has_card = true;
            }
        }
    }

    /* Won with passed card check */
    if (player_play_idx[winner] >= 0) {
        Card winner_card = trick->cards[player_play_idx[winner]];
        if (is_received_card(wi, winner_card))
            wi->won_with_passed_card = true;
    }

    /* --- Section 4: All-player loop --- */
    bool hearts_breaker_found = false;
    bool suit_seen_in_trick[SUIT_COUNT] = {false};
    for (int i = 0; i < trick->num_played; i++) {
        int p = trick->player_ids[i];
        if (p < 0 || p >= NUM_PLAYERS) continue;
        ContractInstance *ci = &p2->players[p].contract;

        /* Streak reset for non-winners */
        if (p != winner)
            ci->current_streak = 0;

        /* Lead tracking */
        if (p == trick->lead_player)
            ci->led_with_suit[trick->lead_suit] = true;

        /* Hearts break: only the first heart played in the trick gets credit */
        if (!hearts_broken_before && !hearts_breaker_found &&
            trick->cards[i].suit == SUIT_HEARTS) {
            ci->broke_hearts = true;
            hearts_breaker_found = true;
        }

        /* QoS trick lead */
        if (qs_in_trick && p == trick->lead_player)
            ci->led_qs_trick = true;

        /* First-of-suit: only the first card of each suit in play order
         * (considering both round-level and trick-level first appearance) */
        Card c = trick->cards[i];
        if (!p2->round.suit_seen[c.suit] && !suit_seen_in_trick[c.suit] &&
            ci->contract_id >= 0) {
            const ContractDef *cd = phase2_get_contract(ci->contract_id);
            if (cd && cd->condition == COND_PLAY_CARD_FIRST_OF_SUIT &&
                card_equals(c, cd->cond_param.card)) {
                ci->played_card_first_of_suit = true;
            }
        }
        suit_seen_in_trick[c.suit] = true;

        /* Hit tracking (non-winner only) */
        if (p != winner) {
            int pts = card_points(c);
            if (pts > 0) {
                ci->hits_dealt[c.suit]++;

                /* Hit with passed card */
                if (is_received_card(ci, c)) {
                    ci->hit_with_passed_card = true;
                }

                /* Hit with transmuted card */
                if (tti && tti->transmutation_ids[i] >= 0) {
                    const TransmutationDef *td =
                        phase2_get_transmutation(tti->transmutation_ids[i]);
                    if (td && td->custom_points != 0) {
                        ci->hit_with_transmute = true;
                    }
                }
            }
        }
    }

    /* --- Section 5: Update round suit_seen state --- */
    for (int i = 0; i < trick->num_played; i++)
        p2->round.suit_seen[trick->cards[i].suit] = true;
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
        if (cd->cond_param.at_least)
            success = (ci->cards_collected[cd->cond_param.suit] >= cd->cond_param.count);
        else
            success = (ci->cards_collected[cd->cond_param.suit] == cd->cond_param.count);
        break;

    case COND_WIN_N_TRICKS:
        if (cd->cond_param.at_least)
            success = (ci->tricks_won >= cd->cond_param.count);
        else
            success = (ci->tricks_won == cd->cond_param.count);
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

    case COND_WIN_CONSECUTIVE_TRICKS:
        success = (ci->max_streak >= cd->cond_param.count);
        break;

    case COND_HIT_N_WITH_SUIT:
        success = (ci->hits_dealt[cd->cond_param.suit] >= cd->cond_param.count);
        break;

    case COND_LOWEST_SCORE: {
        success = true;
        for (int p = 0; p < NUM_PLAYERS; p++) {
            if (p == player_id) continue;
            if (p2->players[p].contract.points_taken <= ci->points_taken) {
                success = false;
                break;
            }
        }
        break;
    }

    case COND_NEVER_LEAD_SUIT:
        success = !ci->led_with_suit[cd->cond_param.suit];
        break;

    case COND_WIN_TRICK_N:
        success = (ci->tricks_won_mask & (1 << cd->cond_param.trick_num)) != 0;
        break;

    case COND_BREAK_HEARTS:
        success = ci->broke_hearts;
        break;

    case COND_WIN_FIRST_N_TRICKS: {
        int n = cd->cond_param.count;
        uint16_t mask = (uint16_t)((1 << n) - 1); /* bits 0..N-1 */
        success = (ci->tricks_won_mask & mask) == mask;
        break;
    }

    case COND_AVOID_LAST_N_TRICKS: {
        int n = cd->cond_param.count;
        uint16_t mask = (uint16_t)(((1 << n) - 1) << (13 - n)); /* bits (13-N)..12 */
        success = (ci->tricks_won_mask & mask) == 0;
        break;
    }

    case COND_WIN_WITH_PASSED_CARD:
        success = ci->won_with_passed_card;
        break;

    case COND_HIT_WITH_PASSED_CARD:
        success = ci->hit_with_passed_card;
        break;

    case COND_WIN_FIRST_AND_LAST:
        success = (ci->tricks_won_mask & (1 << 0)) != 0 &&
                  (ci->tricks_won_mask & (1 << 12)) != 0;
        break;

    case COND_LEAD_QUEEN_SPADES_TRICK:
        success = ci->led_qs_trick;
        break;

    case COND_SHOOT_THE_MOON:
        success = (ci->points_taken == 26);
        break;

    case COND_PREVENT_MOON:
        success = ci->prevented_moon;
        break;

    case COND_PLAY_CARD_FIRST_OF_SUIT:
        success = ci->played_card_first_of_suit;
        break;

    case COND_HIT_WITH_TRANSMUTE:
        success = ci->hit_with_transmute;
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
