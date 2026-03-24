/* ============================================================
 * @deps-implements: contract_logic.h
 * @deps-requires: contract_logic.h, phase2_state.h, phase2_defs.h,
 *                 transmutation_logic.h (transmute_round_state_init),
 *                 core/trick.h, core/card.h, stdlib.h, string.h
 * @deps-last-changed: 2026-03-22 — Replaced raylib.h with stdlib.h (rand)
 * ============================================================ */

#include "contract_logic.h"

#include <string.h>

#include <stdlib.h>

#include "phase2_defs.h"
#include "transmutation_logic.h"
#include "core/card.h"

/* ---- Helper: reset a single ContractInstance ---- */

static void contract_instance_reset(ContractInstance *ci)
{
    ci->contract_id = -1;
    ci->paired_transmutation_id = -1;
    ci->revealed = false;
    ci->completed = false;
    ci->failed = false;
    memset(ci->cards_collected, 0, sizeof(ci->cards_collected));
    ci->tricks_won = 0;
    ci->points_taken = 0;
    ci->has_card = false;
    ci->tricks_won_mask = 0;
    ci->current_streak = 0;
    ci->max_streak = 0;
    memset(ci->led_with_suit, 0, sizeof(ci->led_with_suit));
    ci->broke_hearts = false;
    ci->led_qs_trick = false;
    ci->played_card_first_of_suit = false;
    for (int j = 0; j < MAX_PASS_CARD_COUNT; j++)
        ci->received_in_pass[j] = CARD_NONE;
    ci->num_received = 0;
    ci->won_with_passed_card = false;
    ci->hit_with_passed_card = false;
    memset(ci->hits_dealt, 0, sizeof(ci->hits_dealt));
    ci->prevented_moon = false;
    ci->hit_with_transmute = false;
}

/* ================================================================
 * State init / round reset
 * ================================================================ */

void contract_state_init(Phase2State *p2)
{
    memset(p2, 0, sizeof(*p2));
    p2->enabled = false;

    for (int i = 0; i < NUM_PLAYERS; i++) {
        for (int c = 0; c < MAX_ACTIVE_CONTRACTS; c++)
            contract_instance_reset(&p2->players[i].contracts[c]);
        p2->players[i].num_active_contracts = 0;
        p2->players[i].num_persistent = 0;

        for (int s = 0; s < SUIT_COUNT; s++) {
            p2->players[i].queen_ids[s] = -1;
            p2->players[i].jack_ids[s] = -1;
        }

        transmute_inv_init(&p2->players[i].transmute_inv);
        transmute_hand_init(&p2->players[i].hand_transmutes);
    }

    p2->round.contracts_chosen = false;

    p2->last_played_transmute_id = -1;
    p2->last_played_resolved_effect = TEFFECT_NONE;

    for (int i = 0; i < NUM_PLAYERS; i++) {
        p2->anchor_force_suit[i] = -1;
    }
}

void contract_round_reset(Phase2State *p2)
{
    for (int i = 0; i < NUM_PLAYERS; i++) {
        for (int c = 0; c < MAX_ACTIVE_CONTRACTS; c++)
            contract_instance_reset(&p2->players[i].contracts[c]);
        p2->players[i].num_active_contracts = 0;

        transmute_hand_init(&p2->players[i].hand_transmutes);
    }

    p2->round.contracts_chosen = false;
    memset(p2->round.suit_seen, 0, sizeof(p2->round.suit_seen));
    transmute_round_state_init(&p2->round.transmute_round);
    memset(p2->curse_force_hearts, 0, sizeof(p2->curse_force_hearts));
    for (int i = 0; i < NUM_PLAYERS; i++) {
        p2->anchor_force_suit[i] = -1;
        p2->binding_auto_win[i] = 0;
    }
}

bool contract_all_chosen(const Phase2State *p2)
{
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (p2->players[i].num_active_contracts < MAX_ACTIVE_CONTRACTS)
            return false;
    }
    return true;
}

/* ================================================================
 * Draft system
 * ================================================================ */

void draft_generate_pool(DraftState *draft)
{
    memset(draft, 0, sizeof(*draft));
    draft->active = true;
    draft->current_round = 0;
    draft->timer = DRAFT_TIMER_SECONDS;

    /* --- Select 16 contracts weighted by tier --- */
    /* Partition available contracts by tier */
    int tier_ids[3][MAX_CONTRACT_DEFS];
    int tier_counts[3] = {0, 0, 0};

    for (int i = 0; i < g_contract_def_count; i++) {
        int t = g_contract_defs[i].tier;
        if (t >= 0 && t < 3)
            tier_ids[t][tier_counts[t]++] = g_contract_defs[i].id;
    }

    /* Target: 10 easy (60%), 5 medium (30%), 1 hard (10%) */
    int targets[3] = {10, 5, 1};

    /* Clamp to available and redistribute surplus */
    for (int t = 0; t < 3; t++) {
        if (targets[t] > tier_counts[t]) {
            int surplus = targets[t] - tier_counts[t];
            targets[t] = tier_counts[t];
            /* Give surplus to next tier, or previous if last */
            int next = (t < 2) ? t + 1 : t - 1;
            if (next >= 0 && next < 3)
                targets[next] += surplus;
        }
    }

    /* Shuffle each tier and take first N */
    int selected_contracts[DRAFT_POOL_SIZE];
    int sel_count = 0;

    for (int t = 0; t < 3 && sel_count < DRAFT_POOL_SIZE; t++) {
        /* Fisher-Yates shuffle */
        for (int i = tier_counts[t] - 1; i > 0; i--) {
            int j = (rand() % (i + 1));
            int tmp = tier_ids[t][i];
            tier_ids[t][i] = tier_ids[t][j];
            tier_ids[t][j] = tmp;
        }
        int take = targets[t];
        if (take > tier_counts[t]) take = tier_counts[t];
        if (sel_count + take > DRAFT_POOL_SIZE) take = DRAFT_POOL_SIZE - sel_count;
        for (int i = 0; i < take; i++)
            selected_contracts[sel_count++] = tier_ids[t][i];
    }

    /* Fill remaining if we don't have 16 (shouldn't happen with 29 contracts) */
    if (sel_count > 0) {
        int base = sel_count;
        while (sel_count < DRAFT_POOL_SIZE) {
            selected_contracts[sel_count] = selected_contracts[(sel_count - base) % base];
            sel_count++;
        }
    }

    /* --- Select 16 random transmutations (no duplication) --- */
    /* Excluded IDs: transmutations not yet ready for the draft pool */
    static const int EXCLUDED_TMUTE_IDS[] = { 11 /* The Echo */ };
    static const int NUM_EXCLUDED = sizeof(EXCLUDED_TMUTE_IDS) / sizeof(EXCLUDED_TMUTE_IDS[0]);

    int all_transmutes[MAX_TRANSMUTATION_DEFS];
    int tmute_total = 0;
    for (int i = 0; i < g_transmutation_def_count; i++) {
        int id = g_transmutation_defs[i].id;
        bool excluded = false;
        for (int e = 0; e < NUM_EXCLUDED; e++) {
            if (id == EXCLUDED_TMUTE_IDS[e]) { excluded = true; break; }
        }
        if (!excluded)
            all_transmutes[tmute_total++] = id;
    }

    /* Fisher-Yates shuffle */
    for (int i = tmute_total - 1; i > 0; i--) {
        int j = (rand() % (i + 1));
        int tmp = all_transmutes[i];
        all_transmutes[i] = all_transmutes[j];
        all_transmutes[j] = tmp;
    }

    int tmute_count = tmute_total < DRAFT_POOL_SIZE ? tmute_total : DRAFT_POOL_SIZE;

    /* --- Pair and distribute --- */
    for (int p = 0; p < NUM_PLAYERS; p++) {
        draft->players[p].available_count = DRAFT_GROUP_SIZE;
        draft->players[p].pick_count = 0;
        draft->players[p].has_picked_this_round = false;

        for (int i = 0; i < DRAFT_GROUP_SIZE; i++) {
            int idx = p * DRAFT_GROUP_SIZE + i;
            draft->players[p].available[i].contract_id = selected_contracts[idx];
            draft->players[p].available[i].transmutation_id =
                (idx < tmute_count) ? all_transmutes[idx] : -1;
        }
    }
}

void draft_pick(DraftState *draft, int player_id, int pair_index)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    DraftPlayerState *ps = &draft->players[player_id];
    if (ps->has_picked_this_round) return;
    if (pair_index < 0 || pair_index >= ps->available_count) return;
    if (ps->pick_count >= DRAFT_PICKS_PER_PLAYER) return;

    ps->picked[ps->pick_count++] = ps->available[pair_index];
    ps->has_picked_this_round = true;

    /* Remove picked pair by shifting */
    for (int i = pair_index; i < ps->available_count - 1; i++)
        ps->available[i] = ps->available[i + 1];
    ps->available_count--;
}

void draft_auto_pick(DraftState *draft, int player_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    DraftPlayerState *ps = &draft->players[player_id];
    if (ps->has_picked_this_round || ps->available_count <= 0) return;
    draft_pick(draft, player_id, 0);
}

void draft_ai_pick(DraftState *draft, int player_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    DraftPlayerState *ps = &draft->players[player_id];
    if (ps->has_picked_this_round || ps->available_count <= 0) return;

    int idx = (rand() % ps->available_count);
    draft_pick(draft, player_id, idx);
}

bool draft_all_picked(const DraftState *draft)
{
    for (int p = 0; p < NUM_PLAYERS; p++) {
        if (!draft->players[p].has_picked_this_round)
            return false;
    }
    return true;
}

void draft_advance_round(DraftState *draft)
{
    draft->current_round++;

    if (draft->current_round >= DRAFT_ROUNDS) {
        draft->active = false;
        return;
    }

    /* Rotate remaining pairs clockwise: player p gets player (p-1)'s leftovers */
    DraftPlayerState saved[NUM_PLAYERS];
    for (int p = 0; p < NUM_PLAYERS; p++)
        saved[p] = draft->players[p];

    for (int p = 0; p < NUM_PLAYERS; p++) {
        int src = (p + NUM_PLAYERS - 1) % NUM_PLAYERS; /* clockwise = receive from left */
        draft->players[p].available_count = saved[src].available_count;
        for (int i = 0; i < saved[src].available_count; i++)
            draft->players[p].available[i] = saved[src].available[i];
        draft->players[p].has_picked_this_round = false;
        /* Preserve picked[] and pick_count */
        draft->players[p].pick_count = saved[p].pick_count;
        for (int i = 0; i < saved[p].pick_count; i++)
            draft->players[p].picked[i] = saved[p].picked[i];
    }

    draft->timer = DRAFT_TIMER_SECONDS;
}

void draft_finalize(DraftState *draft, Phase2State *p2)
{
    for (int p = 0; p < NUM_PLAYERS; p++) {
        DraftPlayerState *ps = &draft->players[p];
        PlayerPhase2 *pp = &p2->players[p];

        int n = ps->pick_count;
        if (n > MAX_ACTIVE_CONTRACTS) n = MAX_ACTIVE_CONTRACTS;

        for (int c = 0; c < n; c++) {
            contract_instance_reset(&pp->contracts[c]);
            pp->contracts[c].contract_id = ps->picked[c].contract_id;
            pp->contracts[c].paired_transmutation_id = ps->picked[c].transmutation_id;
        }
        pp->num_active_contracts = n;
    }
    p2->round.contracts_chosen = true;
    draft->active = false;
}

bool draft_is_complete(const DraftState *draft)
{
    return draft->current_round >= DRAFT_ROUNDS;
}

/* ================================================================
 * Contract tracking (multi-contract)
 * ================================================================ */

void contract_record_received_cards(Phase2State *p2, int player_id,
                                    const Card cards[], int count)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    PlayerPhase2 *pp = &p2->players[player_id];
    int n = count < MAX_PASS_CARD_COUNT ? count : MAX_PASS_CARD_COUNT;

    for (int c = 0; c < pp->num_active_contracts; c++) {
        ContractInstance *ci = &pp->contracts[c];
        for (int i = 0; i < n; i++)
            ci->received_in_pass[i] = cards[i];
        ci->num_received = n;
    }
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

    /* --- Pre-compute --- */
    int trick_scoring_pts = 0;
    bool qs_in_trick = false;
    for (int i = 0; i < trick->num_played; i++) {
        trick_scoring_pts += card_points(trick->cards[i]);
        if (card_equals(trick->cards[i], QOS))
            qs_in_trick = true;
    }

    int player_play_idx[NUM_PLAYERS];
    for (int p = 0; p < NUM_PLAYERS; p++)
        player_play_idx[p] = -1;
    for (int i = 0; i < trick->num_played; i++)
        player_play_idx[trick->player_ids[i]] = i;

    /* --- PREVENT_MOON (check across all contracts) --- */
    if (trick_scoring_pts > 0) {
        int total_distributed = 0;
        for (int p = 0; p < NUM_PLAYERS; p++) {
            /* Use first contract's points_taken as representative */
            if (p2->players[p].num_active_contracts > 0)
                total_distributed += p2->players[p].contracts[0].points_taken;
        }

        for (int p = 0; p < NUM_PLAYERS; p++) {
            if (p == winner) continue;
            int pts = (p2->players[p].num_active_contracts > 0)
                          ? p2->players[p].contracts[0].points_taken : 0;
            if (pts > 0 && pts == total_distributed) {
                for (int c = 0; c < p2->players[winner].num_active_contracts; c++)
                    p2->players[winner].contracts[c].prevented_moon = true;
                break;
            }
        }
    }

    /* --- Winner tracking (all contracts) --- */
    PlayerPhase2 *wp = &p2->players[winner];
    for (int c = 0; c < wp->num_active_contracts; c++) {
        ContractInstance *wi = &wp->contracts[c];
        wi->tricks_won++;
        wi->tricks_won_mask |= (uint16_t)(1 << trick_number);
        wi->current_streak++;
        if (wi->current_streak > wi->max_streak)
            wi->max_streak = wi->current_streak;

        for (int i = 0; i < trick->num_played; i++) {
            Card card = trick->cards[i];
            wi->cards_collected[card.suit]++;
            wi->points_taken += card_points(card);

            if (wi->contract_id >= 0) {
                const ContractDef *cd = phase2_get_contract(wi->contract_id);
                if (cd && (cd->condition == COND_COLLECT_CARD ||
                           cd->condition == COND_AVOID_CARD) &&
                    card_equals(card, cd->cond_param.card)) {
                    wi->has_card = true;
                }
            }
        }

        if (player_play_idx[winner] >= 0) {
            Card winner_card = trick->cards[player_play_idx[winner]];
            if (is_received_card(wi, winner_card))
                wi->won_with_passed_card = true;
        }
    }

    /* --- All-player loop (all contracts) --- */
    bool hearts_breaker_found = false;
    bool suit_seen_in_trick[SUIT_COUNT] = {false};
    for (int i = 0; i < trick->num_played; i++) {
        int p = trick->player_ids[i];
        if (p < 0 || p >= NUM_PLAYERS) continue;
        PlayerPhase2 *pp = &p2->players[p];

        for (int c_idx = 0; c_idx < pp->num_active_contracts; c_idx++) {
            ContractInstance *ci = &pp->contracts[c_idx];

            if (p != winner)
                ci->current_streak = 0;

            if (p == trick->lead_player)
                ci->led_with_suit[trick->lead_suit] = true;

            if (!hearts_broken_before && !hearts_breaker_found &&
                trick->cards[i].suit == SUIT_HEARTS) {
                ci->broke_hearts = true;
            }

            if (qs_in_trick && p == trick->lead_player)
                ci->led_qs_trick = true;

            Card card = trick->cards[i];
            if (!p2->round.suit_seen[card.suit] && !suit_seen_in_trick[card.suit] &&
                ci->contract_id >= 0) {
                const ContractDef *cd = phase2_get_contract(ci->contract_id);
                if (cd && cd->condition == COND_PLAY_CARD_FIRST_OF_SUIT &&
                    card_equals(card, cd->cond_param.card)) {
                    ci->played_card_first_of_suit = true;
                }
            }

            if (p != winner) {
                int pts = card_points(card);
                if (pts > 0) {
                    ci->hits_dealt[card.suit]++;
                    if (is_received_card(ci, card))
                        ci->hit_with_passed_card = true;
                    if (tti && tti->transmutation_ids[i] >= 0) {
                        const TransmutationDef *td =
                            phase2_get_transmutation(tti->transmutation_ids[i]);
                        if (td && td->custom_points != 0)
                            ci->hit_with_transmute = true;
                    }
                }
            }
        }

        /* Track hearts breaker only once per trick (not per contract) */
        if (!hearts_broken_before && !hearts_breaker_found &&
            trick->cards[i].suit == SUIT_HEARTS) {
            hearts_breaker_found = true;
        }

        suit_seen_in_trick[trick->cards[i].suit] = true;
    }

    /* Update round suit_seen state */
    for (int i = 0; i < trick->num_played; i++)
        p2->round.suit_seen[trick->cards[i].suit] = true;
}

/* ================================================================
 * Contract evaluation (multi-contract)
 * ================================================================ */

/* Evaluate a single contract instance */
static void contract_evaluate_one(Phase2State *p2, int player_id,
                                   ContractInstance *ci)
{
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
            /* Compare using first contract's points (all track same trick data) */
            int other_pts = (p2->players[p].num_active_contracts > 0)
                                ? p2->players[p].contracts[0].points_taken : 0;
            if (other_pts <= ci->points_taken) {
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
        uint16_t mask = (uint16_t)((1 << n) - 1);
        success = (ci->tricks_won_mask & mask) == mask;
        break;
    }

    case COND_AVOID_LAST_N_TRICKS: {
        int n = cd->cond_param.count;
        uint16_t mask = (uint16_t)(((1 << n) - 1) << (13 - n));
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

    if (success)
        ci->completed = true;
    else
        ci->failed = true;
}

void contract_evaluate_all(Phase2State *p2, int player_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    PlayerPhase2 *pp = &p2->players[player_id];

    for (int c = 0; c < pp->num_active_contracts; c++)
        contract_evaluate_one(p2, player_id, &pp->contracts[c]);
}

void contract_apply_rewards_all(Phase2State *p2, int player_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    PlayerPhase2 *pp = &p2->players[player_id];

    for (int c = 0; c < pp->num_active_contracts; c++) {
        ContractInstance *ci = &pp->contracts[c];
        if (!ci->completed) continue;

        /* Grant the draft-paired transmutation */
        if (ci->paired_transmutation_id >= 0)
            transmute_inv_add(&pp->transmute_inv, ci->paired_transmutation_id);

        /* Add permanent effects from contract def rewards */
        const ContractDef *cd = phase2_get_contract(ci->contract_id);
        if (!cd) continue;

        for (int r = 0; r < cd->num_rewards; r++) {
            if (pp->num_persistent >= MAX_ACTIVE_EFFECTS) break;
            ActiveEffect *ae = &pp->persistent_effects[pp->num_persistent++];
            ae->effect = cd->rewards[r];
            ae->scope = cd->reward_scope;
            ae->source_player = player_id;
            ae->target_player = -1;
            ae->rounds_remaining = -1;
            ae->active = true;
        }
    }
}
