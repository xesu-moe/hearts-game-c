/* ============================================================
 * @deps-implements: ai_competitive.h
 * @deps-requires: server_game.h (ServerGame), core/card.h,
 *                 core/hand.h (Hand), core/trick.h (Trick),
 *                 core/game_state.h, phase2/phase2_state.h,
 *                 phase2/phase2_defs.h, phase2/contract.h,
 *                 phase2/transmutation.h, phase2/transmutation_logic.h
 * @deps-last-changed: 2026-04-02 — Implements competitive AI heuristics
 * ============================================================ */

#include "ai_competitive.h"
#include "server_game.h"

#include "core/card.h"
#include "core/hand.h"
#include "core/trick.h"
#include "core/game_state.h"
#include "phase2/phase2_state.h"
#include "phase2/phase2_defs.h"
#include "phase2/contract.h"
#include "phase2/transmutation.h"
#include "phase2/transmutation_logic.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Tunable Defaults
 * ================================================================ */

static const AIWeights DEFAULT_WEIGHTS = {
    .w_safety  = {  5.0f,  7.0f, 10.0f },
    .w_void    = {  8.0f,  5.0f,  2.0f },
    .w_shed    = {  6.0f,  7.0f,  4.0f },
    .w_queen   = {  9.0f,  9.0f,  9.0f },
    .w_moon    = {  3.0f,  6.0f,  8.0f },
    .w_target  = {  2.0f,  3.0f,  3.0f },
    .w_info    = {  4.0f,  1.0f,  0.0f },
    .w_endgame = {  0.0f,  3.0f,  8.0f },
};

static const AIThresholds DEFAULT_THRESHOLDS = {
    .moon_score_threshold   = 14,
    .moon_commit_threshold  = 18,
    .moon_alarm_with_queen  = 5,
    .moon_alarm_no_queen    = 8,
    .spade_buffer_safe      = 4,
    .spade_buffer_min       = 3,
    .low_heart_max_rank     = 6,   /* RANK_6 */
    .endgame_start_trick    = 10,
    .desperate_score_gap    = 15,
    .cooperative_moon_lead  = 30,
};

/* ================================================================
 * Helpers
 * ================================================================ */

static int game_phase_index(int tricks_played)
{
    if (tricks_played < 4)  return 0; /* early */
    if (tricks_played < 9)  return 1; /* mid */
    return 2;                          /* late */
}

static int count_suit(const Hand *hand, Suit suit)
{
    int n = 0;
    for (int i = 0; i < hand->count; i++)
        if (hand->cards[i].suit == suit) n++;
    return n;
}

static bool hand_has_card(const Hand *hand, Suit suit, Rank rank)
{
    for (int i = 0; i < hand->count; i++)
        if (hand->cards[i].suit == suit && hand->cards[i].rank == rank)
            return true;
    return false;
}

static int min_opponent_score(const CompetitiveAIState *ai)
{
    int best = 9999;
    for (int p = 0; p < NUM_PLAYERS; p++) {
        if (p == ai->my_seat) continue;
        if (ai->game_score[p] < best) best = ai->game_score[p];
    }
    return best;
}

/* Find the game leader (lowest score, not us) */
static int find_leader(const CompetitiveAIState *ai)
{
    int leader = -1, best = 9999;
    for (int p = 0; p < NUM_PLAYERS; p++) {
        if (p == ai->my_seat) continue;
        if (ai->game_score[p] < best) {
            best = ai->game_score[p];
            leader = p;
        }
    }
    return leader;
}

static bool is_qs(Card c) { return c.suit == SUIT_SPADES && c.rank == RANK_Q; }
static bool is_point_card(Card c) { return c.suit == SUIT_HEARTS || is_qs(c); }

/* Current trick winner so far */
static int current_trick_winner(const Trick *trick)
{
    if (trick->num_played == 0) return -1;
    Suit lead = trick->cards[0].suit;
    int best_idx = 0;
    for (int i = 1; i < trick->num_played; i++) {
        if (trick->cards[i].suit == lead &&
            trick->cards[i].rank > trick->cards[best_idx].rank)
            best_idx = i;
    }
    return trick->player_ids[best_idx];
}

/* Points currently in the trick */
static int current_trick_points(const Trick *trick)
{
    int pts = 0;
    for (int i = 0; i < trick->num_played; i++)
        pts += card_points(trick->cards[i]);
    return pts;
}

/* Would this card win the current trick? */
static bool would_win_trick(const Trick *trick, Card card)
{
    if (trick->num_played == 0) return true; /* leading */
    Suit lead = trick->cards[0].suit;
    if (card.suit != lead) return false; /* off-suit never wins */
    for (int i = 0; i < trick->num_played; i++) {
        if (trick->cards[i].suit == lead && trick->cards[i].rank >= card.rank)
            return false;
    }
    return true;
}

/* Highest card in trick of the lead suit */
static Rank current_winning_rank(const Trick *trick)
{
    if (trick->num_played == 0) return 0;
    Suit lead = trick->cards[0].suit;
    Rank best = 0;
    for (int i = 0; i < trick->num_played; i++) {
        if (trick->cards[i].suit == lead && trick->cards[i].rank > best)
            best = trick->cards[i].rank;
    }
    return best;
}

/* ================================================================
 * Moon Hand Evaluation (Section 7.1)
 * ================================================================ */

static int eval_moon_hand(const Hand *hand)
{
    int score = 0;
    int heart_count = 0;
    for (int i = 0; i < hand->count; i++) {
        Card c = hand->cards[i];
        if (c.suit == SUIT_HEARTS) {
            heart_count++;
            if (c.rank == RANK_A)      score += 3;
            else if (c.rank == RANK_K) score += 2;
            else if (c.rank == RANK_Q) score += 2;
            else if (c.rank == RANK_J) score += 1;
        } else if (is_qs(c)) {
            score += 3;
        } else if (c.suit == SUIT_SPADES && c.rank == RANK_A) {
            score += 2;
        } else if (c.suit == SUIT_SPADES && c.rank == RANK_K) {
            score += 1;
        } else if (c.rank == RANK_A) {
            score += 2;
        } else if (c.rank == RANK_K) {
            score += 1;
        } else if (c.rank <= RANK_5) {
            /* Cards below 5 (non-heart) are weak */
        }
    }
    /* Extra hearts bonus */
    score += heart_count / 2; /* +0.5 per extra heart, integer approx */
    return score;
}

/* ================================================================
 * Moon Detection (Section 7.3)
 * ================================================================ */

static void detect_moon(CompetitiveAIState *ai)
{
    const AIThresholds *t = &DEFAULT_THRESHOLDS;
    ai->moon_alarm = false;
    ai->moon_suspect = -1;

    for (int p = 0; p < NUM_PLAYERS; p++) {
        if (p == ai->my_seat) continue;
        int h = ai->hearts_taken[p];
        bool q = ai->has_queen[p];
        if ((q && h >= t->moon_alarm_with_queen) ||
            (!q && h >= t->moon_alarm_no_queen)) {
            ai->moon_alarm = true;
            ai->moon_suspect = p;
            return;
        }
    }
}

/* ================================================================
 * Lifecycle: comp_ai_init_hand
 * ================================================================ */

void comp_ai_init_hand(CompetitiveAIState *ai, int seat,
                       const GameState *gs)
{
    memset(ai, 0, sizeof(*ai));
    ai->my_seat = seat;
    ai->moon_suspect = -1;

    /* Init suit counts */
    for (int s = 0; s < SUIT_COUNT; s++)
        ai->remaining_in_suit[s] = 13;

    /* Copy game scores */
    for (int p = 0; p < NUM_PLAYERS; p++)
        ai->game_score[p] = gs->players[p].total_score;

    /* Check QS location */
    const Hand *hand = &gs->players[seat].hand;
    if (hand_has_card(hand, SUIT_SPADES, RANK_Q))
        ai->qs_status = QS_IN_MY_HAND;
    else
        ai->qs_status = QS_LOCATION_UNKNOWN;

    /* Record active contracts */
    ai->num_active_contracts = 0;
    /* Contracts are populated after draft — updated separately */
}

/* ================================================================
 * Lifecycle: comp_ai_on_trick_complete
 * ================================================================ */

void comp_ai_on_trick_complete(CompetitiveAIState *ai,
                               const Trick *trick, int winner)
{
    if (!trick || trick->num_played < CARDS_PER_TRICK) return;

    Suit lead_suit = trick->cards[0].suit;

    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        Card c = trick->cards[i];
        int pid = trick->player_ids[i];

        /* 1.1 — Mark played */
        int idx = card_to_index(c);
        if (idx >= 0 && idx < DECK_SIZE)
            ai->played[idx] = true;

        /* 1.2 — Void detection */
        if (c.suit != lead_suit && pid >= 0 && pid < NUM_PLAYERS)
            ai->player_void[pid][lead_suit] = true;

        /* 1.4 — Decrement suit count */
        if (c.suit >= 0 && c.suit < SUIT_COUNT)
            ai->remaining_in_suit[c.suit]--;
    }

    /* 1.5 — Track hearts/queen taken by winner */
    if (winner >= 0 && winner < NUM_PLAYERS) {
        for (int i = 0; i < CARDS_PER_TRICK; i++) {
            Card c = trick->cards[i];
            if (c.suit == SUIT_HEARTS)
                ai->hearts_taken[winner]++;
            if (is_qs(c))
                ai->has_queen[winner] = true;
        }
    }

    /* 1.3 — QS tracking */
    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        if (is_qs(trick->cards[i])) {
            ai->qs_status = QS_PLAYED;
            break;
        }
    }

    /* Moon detection */
    detect_moon(ai);

    /* Moon abort: if we're attempting and someone else got a heart */
    if (ai->attempting_moon && winner != ai->my_seat) {
        for (int i = 0; i < CARDS_PER_TRICK; i++) {
            if (trick->cards[i].suit == SUIT_HEARTS) {
                ai->attempting_moon = false;
                break;
            }
        }
    }
}

/* ================================================================
 * Lifecycle: comp_ai_on_pass_execute
 * ================================================================ */

void comp_ai_on_pass_execute(CompetitiveAIState *ai,
                             const Card *sent, int sent_count,
                             const Card *received, int recv_count,
                             PassDirection dir, int pass_target)
{
    ai->pass_direction = dir;
    ai->num_passed = sent_count;
    ai->num_received = recv_count;
    for (int i = 0; i < sent_count && i < MAX_PASS_CARD_COUNT; i++)
        ai->cards_passed[i] = sent[i];
    for (int i = 0; i < recv_count && i < MAX_PASS_CARD_COUNT; i++)
        ai->cards_received[i] = received[i];

    /* QS tracking through pass */
    for (int i = 0; i < sent_count; i++) {
        if (is_qs(sent[i])) {
            ai->qs_status = QS_PASSED_TO;
            ai->qs_related_player = pass_target;
            return;
        }
    }
    for (int i = 0; i < recv_count; i++) {
        if (is_qs(received[i])) {
            ai->qs_status = QS_RECEIVED_FROM;
            /* We don't know exactly who passed to us unless dir is known */
            ai->qs_related_player = -1;
            return;
        }
    }
}

/* ================================================================
 * Dealer Decisions (Phase C1)
 * ================================================================ */

PassDirection comp_ai_pick_direction(const CompetitiveAIState *ai,
                                     const GameState *gs)
{
    /* Pass LEFT to the game leader (hurts them most) */
    int leader = find_leader(ai);
    if (leader >= 0) {
        int my = ai->my_seat;
        /* LEFT = player (my+1)%4 */
        if (leader == (my + 1) % NUM_PLAYERS) return PASS_LEFT;
        /* RIGHT = player (my+3)%4 */
        if (leader == (my + 3) % NUM_PLAYERS) return PASS_RIGHT;
        /* ACROSS = player (my+2)%4 */
        if (leader == (my + 2) % NUM_PLAYERS) return PASS_ACROSS;
    }
    (void)gs;
    return PASS_LEFT; /* default */
}

int comp_ai_pick_amount(const CompetitiveAIState *ai,
                        const GameState *gs)
{
    /* Evaluate hand quality: count high cards (rank >= J) */
    const Hand *hand = &gs->players[ai->my_seat].hand;
    int high_count = 0;
    for (int i = 0; i < hand->count; i++) {
        if (hand->cards[i].rank >= RANK_J) high_count++;
    }
    /* Weak hand: pass more to fix it */
    if (high_count >= 6) return 4;
    if (high_count >= 4) return 3;
    return 2;
}

/* ================================================================
 * Draft Decisions (Phase C2)
 * ================================================================ */

/* Intrinsic contract rating by condition type */
static float rate_contract(int contract_id)
{
    const ContractDef *cd = phase2_get_contract(contract_id);
    if (!cd) return 0.0f;

    float score;
    switch (cd->condition) {
    case COND_TAKE_NO_POINTS:       score = 9.0f; break;
    case COND_AVOID_SUIT:
        score = (cd->cond_param.suit == SUIT_HEARTS) ? 8.0f : 5.0f;
        break;
    case COND_AVOID_CARD:           score = 8.0f; break;
    case COND_LOWEST_SCORE:         score = 7.0f; break;
    case COND_WIN_N_TRICKS:
        score = (cd->cond_param.count <= 4) ? 6.0f : 4.0f;
        break;
    case COND_COLLECT_N_OF_SUIT:    score = 5.0f; break;
    case COND_AVOID_LAST_N_TRICKS:  score = 5.0f; break;
    case COND_BREAK_HEARTS:         score = 4.0f; break;
    case COND_WIN_WITH_PASSED_CARD: score = 4.0f; break;
    case COND_HIT_WITH_PASSED_CARD: score = 4.0f; break;
    case COND_HIT_N_WITH_SUIT:      score = 4.0f; break;
    case COND_NEVER_LEAD_SUIT:      score = 3.0f; break;
    case COND_WIN_CONSECUTIVE_TRICKS:score = 3.0f; break;
    case COND_WIN_TRICK_N:          score = 3.0f; break;
    case COND_TAKE_EXACT_POINTS:    score = 3.0f; break;
    case COND_COLLECT_CARD:         score = 3.0f; break;
    case COND_WIN_FIRST_N_TRICKS:   score = 2.0f; break;
    case COND_SHOOT_THE_MOON:       score = 2.0f; break;
    case COND_WIN_FIRST_AND_LAST:   score = 2.0f; break;
    case COND_LEAD_QUEEN_SPADES_TRICK: score = 1.0f; break;
    case COND_PREVENT_MOON:         score = 4.0f; break;
    case COND_PLAY_CARD_FIRST_OF_SUIT: score = 3.0f; break;
    case COND_HIT_WITH_TRANSMUTE:   score = 3.0f; break;
    default:                        score = 3.0f; break;
    }

    /* Tier discount: easy=1.0, medium=0.8, hard=0.6 */
    float tier_mult = 1.0f - 0.2f * cd->tier;
    return score * tier_mult;
}

/* Intrinsic transmutation rating */
static float rate_transmutation(int transmute_id)
{
    const TransmutationDef *td = phase2_get_transmutation(transmute_id);
    if (!td) return 0.0f;

    switch (td->effect) {
    case TEFFECT_INVERSION_NEGATE_POINTS:   return 9.0f;
    case TEFFECT_WOTT_SHIELD_NEXT_TRICK:    return 9.0f;
    case TEFFECT_WOTT_REDUCE_SCORE_3:       return 8.0f;
    case TEFFECT_WOTT_REDUCE_SCORE_1:       return 7.0f;
    case TEFFECT_JOKER_LEAD_WIN:            return 7.0f;
    case TEFFECT_FOG_HIDDEN:                return 6.0f;
    case TEFFECT_BOUNTY_REDIRECT_QOS:       return 6.0f;
    case TEFFECT_WOTT_REVEAL_OPPONENT_CARD: return 5.0f;
    case TEFFECT_CROWN_HIGHEST_RANK:        return 5.0f;
    case TEFFECT_WOTT_SWAP_CARD:            return 5.0f;
    case TEFFECT_PARASITE_REDIRECT_POINTS:  return 4.0f;
    case TEFFECT_MIRROR:                    return 4.0f;
    case TEFFECT_TRAP_DOUBLE_WITH_QOS:      return 3.0f;
    case TEFFECT_ANCHOR_FORCE_LEAD_SUIT:    return 3.0f;
    case TEFFECT_WOTT_FORCE_LEAD_HEARTS:    return 3.0f;
    case TEFFECT_BINDING_AUTO_WIN_NEXT:     return 2.0f;
    case TEFFECT_RANDOM_TRICK_WINNER:       return 2.0f;
    case TEFFECT_WOTT_DUPLICATE_ROUND_POINTS: return 1.0f;
    default: return 3.0f;
    }
}

int comp_ai_draft_pick(const DraftPlayerState *dps,
                       const Phase2State *p2)
{
    (void)p2;
    if (dps->available_count <= 0) return 0;

    int best_idx = 0;
    float best_score = -999.0f;

    for (int i = 0; i < dps->available_count; i++) {
        int cid = dps->available[i].contract_id;
        int tid = dps->available[i].transmutation_id;
        float score = rate_contract(cid) + rate_transmutation(tid);
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    return best_idx;
}

/* ================================================================
 * Pass Selection (Section 2)
 * ================================================================ */

void comp_ai_select_pass(CompetitiveAIState *ai,
                         const GameState *gs,
                         Card *out_cards, int pass_count)
{
    const Hand *hand = &gs->players[ai->my_seat].hand;
    if (pass_count <= 0 || hand->count < pass_count) return;

    bool selected[MAX_HAND_SIZE] = {false};
    int out_count = 0;

    /* Check for moon hand */
    int moon_score = eval_moon_hand(hand);
    if (moon_score >= DEFAULT_THRESHOLDS.moon_score_threshold) {
        ai->attempting_moon = true;
        /* Moon pass: pass lowest cards in weakest suit */
        int suit_counts[SUIT_COUNT] = {0};
        for (int i = 0; i < hand->count; i++)
            suit_counts[hand->cards[i].suit]++;

        /* Find weakest non-heart suit */
        Suit weak_suit = (Suit)-1;
        int weak_count = 99;
        for (int s = 0; s < SUIT_COUNT; s++) {
            if (s == SUIT_HEARTS) continue;
            if (suit_counts[s] > 0 && suit_counts[s] < weak_count) {
                weak_count = suit_counts[s];
                weak_suit = s;
            }
        }
        if (weak_suit >= 0) {
            for (int i = 0; i < hand->count && out_count < pass_count; i++) {
                if (hand->cards[i].suit == weak_suit) {
                    out_cards[out_count++] = hand->cards[i];
                    selected[i] = true;
                }
            }
        }
        /* Fill remaining with lowest non-heart cards */
        for (Rank rank = RANK_2; rank <= RANK_A && out_count < pass_count; rank++) {
            for (int i = 0; i < hand->count && out_count < pass_count; i++) {
                if (selected[i]) continue;
                if (hand->cards[i].suit == SUIT_HEARTS) continue;
                if (hand->cards[i].rank == rank) {
                    out_cards[out_count++] = hand->cards[i];
                    selected[i] = true;
                }
            }
        }
        if (out_count >= pass_count) return;
    }

    /* Normal pass logic */
    ai->attempting_moon = false;

    int spade_count = count_suit(hand, SUIT_SPADES);
    bool have_qs = hand_has_card(hand, SUIT_SPADES, RANK_Q);
    PassDirection dir = gs->pass_direction;

    /* 2.1 — Q♠ decision */
    if (have_qs && out_count < pass_count) {
        bool keep_qs = false;
        if (spade_count >= DEFAULT_THRESHOLDS.spade_buffer_safe) keep_qs = true;
        if (dir == PASS_LEFT && spade_count >= DEFAULT_THRESHOLDS.spade_buffer_min) keep_qs = true;

        if (!keep_qs) {
            for (int i = 0; i < hand->count; i++) {
                if (selected[i]) continue;
                if (is_qs(hand->cards[i])) {
                    out_cards[out_count++] = hand->cards[i];
                    selected[i] = true;
                    break;
                }
            }
        }
    }

    /* 2.2 — A♠/K♠ decision */
    if (!have_qs && out_count < pass_count) {
        int low_spades = 0;
        for (int i = 0; i < hand->count; i++) {
            if (hand->cards[i].suit == SUIT_SPADES && hand->cards[i].rank < RANK_Q)
                low_spades++;
        }
        if (low_spades < 3) {
            /* Pass A♠ and K♠ if we have them */
            Rank dangerous[] = {RANK_A, RANK_K};
            for (int d = 0; d < 2 && out_count < pass_count; d++) {
                for (int i = 0; i < hand->count; i++) {
                    if (selected[i]) continue;
                    if (hand->cards[i].suit == SUIT_SPADES &&
                        hand->cards[i].rank == dangerous[d]) {
                        out_cards[out_count++] = hand->cards[i];
                        selected[i] = true;
                        break;
                    }
                }
            }
        }
    }

    /* 2.3 — Void targeting: find shortest non-spade suit */
    if (out_count < pass_count) {
        Suit void_priority[] = {SUIT_CLUBS, SUIT_DIAMONDS, SUIT_HEARTS};
        for (int si = 0; si < 3 && out_count < pass_count; si++) {
            Suit s = void_priority[si];
            int sc = count_suit(hand, s);
            if (sc == 0 || sc > 3) continue;

            /* For hearts, only void if all high */
            if (s == SUIT_HEARTS) {
                bool all_high = true;
                for (int i = 0; i < hand->count; i++) {
                    if (hand->cards[i].suit == SUIT_HEARTS &&
                        (int)hand->cards[i].rank <= DEFAULT_THRESHOLDS.low_heart_max_rank) {
                        all_high = false;
                        break;
                    }
                }
                if (!all_high) continue;
            }

            /* For clubs, keep at least 1 */
            int min_keep = (s == SUIT_CLUBS) ? 1 : 0;
            int passed_from_suit = 0;

            /* Pass highest cards from this suit first */
            for (Rank rank = RANK_A; rank >= RANK_2 && out_count < pass_count; rank--) {
                if (sc - passed_from_suit <= min_keep) break;
                for (int i = 0; i < hand->count; i++) {
                    if (selected[i]) continue;
                    if (hand->cards[i].suit == s && hand->cards[i].rank == rank) {
                        /* 2.4 — Never pass low hearts */
                        if (s == SUIT_HEARTS &&
                            (int)hand->cards[i].rank <= DEFAULT_THRESHOLDS.low_heart_max_rank)
                            continue;
                        out_cards[out_count++] = hand->cards[i];
                        selected[i] = true;
                        passed_from_suit++;
                        break;
                    }
                }
            }
        }
    }

    /* 2.5 — Dangerous aces in short suits */
    if (out_count < pass_count) {
        for (int s = 0; s < SUIT_COUNT && out_count < pass_count; s++) {
            if (s == SUIT_SPADES) continue; /* handled above */
            int sc = count_suit(hand, (Suit)s);
            if (sc <= 2) {
                for (int i = 0; i < hand->count; i++) {
                    if (selected[i]) continue;
                    if ((int)hand->cards[i].suit == s && hand->cards[i].rank == RANK_A) {
                        out_cards[out_count++] = hand->cards[i];
                        selected[i] = true;
                        break;
                    }
                }
            }
        }
    }

    /* 2.5b — High hearts (J, Q, K, A) if we don't have enough low hearts */
    if (out_count < pass_count) {
        int low_hearts = 0;
        for (int i = 0; i < hand->count; i++) {
            if (hand->cards[i].suit == SUIT_HEARTS &&
                (int)hand->cards[i].rank <= DEFAULT_THRESHOLDS.low_heart_max_rank)
                low_hearts++;
        }
        if (low_hearts < 3) {
            Rank high_ranks[] = {RANK_A, RANK_K, RANK_Q, RANK_J};
            for (int r = 0; r < 4 && out_count < pass_count; r++) {
                for (int i = 0; i < hand->count; i++) {
                    if (selected[i]) continue;
                    if (hand->cards[i].suit == SUIT_HEARTS &&
                        hand->cards[i].rank == high_ranks[r]) {
                        out_cards[out_count++] = hand->cards[i];
                        selected[i] = true;
                        break;
                    }
                }
            }
        }
    }

    /* Fill remaining: highest point value, then highest rank */
    while (out_count < pass_count) {
        int best = -1;
        int best_pts = -1;
        int best_rank = -1;
        for (int i = 0; i < hand->count; i++) {
            if (selected[i]) continue;
            int pts = card_points(hand->cards[i]);
            int rnk = hand->cards[i].rank;
            if (pts > best_pts || (pts == best_pts && rnk > best_rank)) {
                best = i;
                best_pts = pts;
                best_rank = rnk;
            }
        }
        if (best < 0) break;
        out_cards[out_count++] = hand->cards[best];
        selected[best] = true;
    }
}

/* ================================================================
 * Card Evaluation Function (Section 10)
 * ================================================================ */

/* Context for evaluation */
typedef struct EvalCtx {
    const CompetitiveAIState *ai;
    const GameState *gs;
    const Phase2State *p2;
    const Hand *hand;
    int player_id;
    int phase_idx;
    bool leading;
    bool following; /* following suit */
    bool sloughing; /* off-suit */
    bool first_trick;
    Suit lead_suit;
    int trick_points;
    int trick_winner;
    bool is_last_player; /* 4th to play */
} EvalCtx;

/* --- Factor: trick_safety [-10..+10] --- */
static float f_trick_safety(const EvalCtx *ctx, Card card)
{
    if (ctx->leading) {
        /* Leading low is safe */
        if (card.rank <= RANK_5) return 5.0f;
        if (card.rank <= RANK_8) return 2.0f;
        if (card.rank <= RANK_10) return 0.0f;
        return -3.0f;
    }

    if (ctx->sloughing) return 10.0f; /* off-suit never wins */

    /* Following suit */
    const Trick *trick = &ctx->gs->current_trick;
    Rank winning = current_winning_rank(trick);

    if (card.rank < winning) {
        /* Safely ducking: higher duck = better (sheds more dangerous card) */
        return 5.0f + (float)card.rank / (float)RANK_A * 3.0f;
    }

    /* Would win the trick */
    if (ctx->is_last_player && ctx->trick_points == 0) {
        /* Last player, clean trick — safe to take */
        return 4.0f;
    }

    /* Taking a trick with points */
    float penalty = -(float)ctx->trick_points * 0.8f;
    if (penalty < -10.0f) penalty = -10.0f;
    return penalty;
}

/* --- Factor: void_progress [0..+5] --- */
static float f_void_progress(const EvalCtx *ctx, Card card)
{
    int suit_count = count_suit(ctx->hand, card.suit);
    if (suit_count <= 1) return 5.0f;  /* completes void */
    if (suit_count == 2) return 3.0f;  /* second-to-last */
    if (suit_count == 3) return 1.0f;
    return 0.0f;
}

/* --- Factor: dangerous_card_shed [0..+8] --- */
static float f_dangerous_card_shed(const EvalCtx *ctx, Card card)
{
    bool qs_live = (ctx->ai->qs_status != QS_PLAYED);

    if (ctx->sloughing) {
        /* Dump priority (Section 6.1) */
        if (is_qs(card)) return 8.0f;
        if (card.suit == SUIT_SPADES && card.rank == RANK_A && qs_live) return 7.0f;
        if (card.suit == SUIT_SPADES && card.rank == RANK_K && qs_live) return 6.5f;
        if (card.suit == SUIT_HEARTS && card.rank == RANK_A) return 5.0f;
        if (card.suit == SUIT_HEARTS && card.rank >= RANK_K) return 4.5f;
        if (card.suit == SUIT_HEARTS && card.rank >= RANK_Q) return 4.0f;
        if (card.suit == SUIT_HEARTS && card.rank >= RANK_J) return 3.5f;
        if (card.rank == RANK_A) return 2.5f;
        if (card.rank == RANK_K) return 2.0f;
        return 0.0f;
    }

    /* When following, shedding high cards we'd rather not have */
    if (card.suit == SUIT_SPADES && card.rank >= RANK_K && qs_live) return 3.0f;
    if (card.rank == RANK_A && count_suit(ctx->hand, card.suit) <= 2) return 2.0f;
    return 0.0f;
}

/* --- Factor: queen_safety [-10..+5] --- */
static float f_queen_safety(const EvalCtx *ctx, Card card)
{
    if (ctx->ai->qs_status == QS_PLAYED) return 0.0f; /* QS gone */

    if (ctx->following && ctx->lead_suit == SUIT_SPADES) {
        /* Following spades while QS is out */
        if (is_qs(card)) {
            /* Forced to play QS */
            return -8.0f;
        }
        if (ctx->ai->qs_status == QS_IN_MY_HAND) {
            /* We hold QS — play under it */
            if (card.rank < RANK_Q) return 3.0f;
            /* Playing A/K when we hold Q — risky if someone follows with Q */
            return -2.0f;
        }
        /* We don't hold QS */
        if (card.rank >= RANK_K) {
            /* A♠/K♠ could eat the queen */
            return -7.0f;
        }
        /* Low spade — safe */
        return 2.0f;
    }

    if (ctx->leading && card.suit == SUIT_SPADES) {
        if (ctx->ai->qs_status == QS_IN_MY_HAND) {
            /* Leading spades when we hold QS: lead A/K to flush */
            if (card.rank == RANK_A || card.rank == RANK_K) return 4.0f;
            return -2.0f;
        }
        /* We don't hold QS — lead low to flush */
        if (card.rank <= RANK_J) return 3.0f;
        /* Leading A/K without QS — dangerous */
        return -8.0f;
    }

    /* QS is in the current trick — safe to play high spades */
    const Trick *trick = &ctx->gs->current_trick;
    for (int i = 0; i < trick->num_played; i++) {
        if (is_qs(trick->cards[i])) return 5.0f;
    }

    return 0.0f;
}

/* --- Factor: moon_factor [-5..+5] --- */
static float f_moon_factor(const EvalCtx *ctx, Card card)
{
    if (ctx->ai->attempting_moon) {
        /* We want to win heart tricks */
        if (card.suit == SUIT_HEARTS && would_win_trick(&ctx->gs->current_trick, card))
            return 5.0f;
        if (card.rank == RANK_A || card.rank == RANK_K)
            return 2.0f; /* high cards help win tricks */
        return 0.0f;
    }

    if (ctx->ai->moon_alarm) {
        /* Block the moon: take a heart trick if cheap */
        if (card.suit == SUIT_HEARTS && card.rank == RANK_A)
            return 5.0f; /* guaranteed to take and block */
        if (ctx->sloughing && card.suit == SUIT_HEARTS &&
            card.rank <= RANK_4) {
            /* Dump a low heart on a non-moon-player's trick to break sweep */
            if (ctx->trick_winner != ctx->ai->moon_suspect)
                return 4.0f;
        }
        return 0.0f;
    }

    return 0.0f;
}

/* --- Factor: opponent_targeting [0..+3] --- */
static float f_opponent_targeting(const EvalCtx *ctx, Card card)
{
    if (!ctx->sloughing) return 0.0f;
    if (!is_point_card(card)) return 0.0f;

    int leader = find_leader(ctx->ai);
    if (leader >= 0 && ctx->trick_winner == leader)
        return 3.0f;
    if (ctx->trick_winner >= 0 && ctx->trick_winner != ctx->ai->my_seat)
        return 1.0f;
    return 0.0f;
}

/* --- Factor: information_hiding [-3..0] --- */
static float f_information_hiding(const EvalCtx *ctx, Card card)
{
    (void)card;
    if (ctx->sloughing && ctx->gs->tricks_played < 2)
        return -2.0f; /* revealing void early */
    return 0.0f;
}

/* --- Factor: endgame_safety [-5..+5] --- */
static float f_endgame_safety(const EvalCtx *ctx, Card card)
{
    if (ctx->gs->tricks_played < DEFAULT_THRESHOLDS.endgame_start_trick)
        return 0.0f;

    /* Count how many suits we'd have left after playing this card */
    int suits_remaining = 0;
    bool suit_present[SUIT_COUNT] = {false};
    for (int i = 0; i < ctx->hand->count; i++) {
        Card c = ctx->hand->cards[i];
        if (c.suit == card.suit && c.rank == card.rank) continue; /* skip this card */
        suit_present[c.suit] = true;
    }
    for (int s = 0; s < SUIT_COUNT; s++)
        if (suit_present[s]) suits_remaining++;

    if (suits_remaining >= 2) return 3.0f;
    if (suits_remaining == 1) return -3.0f;
    return -5.0f; /* last card = stuck */
}

/* --- Main eval_card --- */
static float eval_card(const EvalCtx *ctx, Card card)
{
    const AIWeights *w = &DEFAULT_WEIGHTS;
    int p = ctx->phase_idx;

    float score = 0.0f;
    float f_safe   = f_trick_safety(ctx, card);
    float f_void   = f_void_progress(ctx, card);
    float f_shed   = f_dangerous_card_shed(ctx, card);
    float f_queen  = f_queen_safety(ctx, card);
    float f_moon   = f_moon_factor(ctx, card);
    float f_tgt    = f_opponent_targeting(ctx, card);
    float f_info   = f_information_hiding(ctx, card);
    float f_end    = f_endgame_safety(ctx, card);

    score += w->w_safety[p]  * f_safe;
    score += w->w_void[p]    * f_void;
    score += w->w_shed[p]    * f_shed;
    score += w->w_queen[p]   * f_queen;
    score += w->w_moon[p]    * f_moon;
    score += w->w_target[p]  * f_tgt;
    score += w->w_info[p]    * f_info;
    score += w->w_endgame[p] * f_end;

    /* Score-based aggression (Section 9) */
    int gap = ctx->ai->game_score[ctx->ai->my_seat] - min_opponent_score(ctx->ai);
    if (gap > DEFAULT_THRESHOLDS.desperate_score_gap) {
        /* Desperate: boost moon and targeting */
        score += w->w_moon[p]   * f_moon * 0.5f;
        score += w->w_target[p] * f_tgt  * 0.5f;
    } else if (gap < -20) {
        /* Winning comfortably: boost safety */
        score += w->w_safety[p] * f_safe * 0.3f;
    }

    return score;
}

/* ================================================================
 * Play Card (Section 4, 5, 6)
 * ================================================================ */

Card comp_ai_play_card(CompetitiveAIState *ai,
                       const struct ServerGame *sg, int player_id)
{
    const GameState *gs = &sg->gs;
    const Phase2State *p2 = &sg->p2;
    const Hand *hand = &gs->players[player_id].hand;
    const Trick *trick = &gs->current_trick;

    bool first_trick = (gs->tricks_played == 0);
    bool leading = (trick->num_played == 0);
    Suit lead_suit = leading ? SUIT_CLUBS : trick->cards[0].suit;
    bool has_lead_suit = false;
    if (!leading) {
        for (int i = 0; i < hand->count; i++) {
            if (hand->cards[i].suit == lead_suit) {
                has_lead_suit = true;
                break;
            }
        }
    }

    bool cursed = p2->enabled && p2->curse_force_hearts[player_id];
    bool anchored = p2->enabled && p2->anchor_force_suit[player_id] >= 0;

    EvalCtx ctx = {
        .ai = ai,
        .gs = gs,
        .p2 = p2,
        .hand = hand,
        .player_id = player_id,
        .phase_idx = game_phase_index(gs->tricks_played),
        .leading = leading,
        .following = !leading && has_lead_suit,
        .sloughing = !leading && !has_lead_suit,
        .first_trick = first_trick,
        .lead_suit = lead_suit,
        .trick_points = current_trick_points(trick),
        .trick_winner = current_trick_winner(trick),
        .is_last_player = (trick->num_played == 3),
    };

    /* Enumerate legal cards and score them */
    float best_score = -99999.0f;
    int best_idx = -1;

    for (int i = 0; i < hand->count; i++) {
        bool valid;
        if (p2->enabled) {
            bool hb = gs->hearts_broken || (leading && cursed);
            valid = transmute_is_valid_play(
                &gs->current_trick, hand,
                &p2->players[player_id].hand_transmutes, i,
                hand->cards[i], hb, first_trick);
            if (leading && cursed && valid)
                valid = transmute_curse_is_valid_lead(hand, hand->cards[i]);
            if (leading && !cursed && anchored && valid)
                valid = transmute_anchor_is_valid_lead(
                    hand, hand->cards[i],
                    p2->anchor_force_suit[player_id]);
        } else {
            valid = game_state_is_valid_play(gs, player_id, hand->cards[i]);
        }
        if (!valid) continue;

        float score = eval_card(&ctx, hand->cards[i]);

        /* Tie-breaking: prefer higher rank, then longer suit */
        if (score > best_score ||
            (score == best_score && best_idx >= 0 &&
             (hand->cards[i].rank > hand->cards[best_idx].rank ||
              (hand->cards[i].rank == hand->cards[best_idx].rank &&
               count_suit(hand, hand->cards[i].suit) >
               count_suit(hand, hand->cards[best_idx].suit))))) {
            best_score = score;
            best_idx = i;
        }
    }

    if (best_idx >= 0)
        return hand->cards[best_idx];

    /* Fallback: first card (should not happen with valid game state) */
    if (hand->count > 0)
        return hand->cards[0];
    return (Card){.suit = SUIT_CLUBS, .rank = RANK_2};
}

/* ================================================================
 * Transmutation Application (Phase C3)
 * ================================================================ */

void comp_ai_apply_transmutations(CompetitiveAIState *ai,
                                  Hand *hand,
                                  HandTransmuteState *hts,
                                  TransmuteInventory *inv,
                                  bool is_passing, int player_id)
{
    (void)ai;
    if (inv->count <= 0) return;

    /* Score each (transmutation, target_card) pair */
    float best_score = -999.0f;
    int best_slot = -1;
    int best_target = -1;

    for (int s = 0; s < inv->count; s++) {
        const TransmutationDef *td = phase2_get_transmutation(inv->items[s]);
        if (!td) continue;

        bool td_is_fog = (td->effect == TEFFECT_FOG_HIDDEN);

        /* During pass: only apply negative transmutations */
        if (is_passing && !td->negative && !td_is_fog) continue;
        /* During play: skip negative transmutations */
        if (!is_passing && td->negative) continue;

        for (int i = 0; i < hand->count; i++) {
            /* Skip restricted cards */
            Card c = hand->cards[i];
            if (c.suit == SUIT_CLUBS && c.rank == RANK_2) continue;
            if (c.suit == SUIT_SPADES && c.rank == RANK_Q && !td_is_fog) continue;

            if (td_is_fog) {
                if (hts->slots[i].fogged) continue;
            } else {
                if (transmute_is_transmuted(hts, i)) continue;
            }

            float score = 0.0f;

            if (is_passing) {
                /* Passing: transmute high-value cards for maximum impact */
                score = (float)c.rank;
                /* Shadow Queen on high cards = devastating */
                if (td->result_suit == SUIT_SPADES && td->result_rank == RANK_Q)
                    score += 5.0f;
                /* Martyr on high cards = doubles opponent's pain */
                if (td->effect == TEFFECT_WOTT_DUPLICATE_ROUND_POINTS)
                    score += 4.0f;
                /* Trap on spade cards = synergizes with QS */
                if (td->effect == TEFFECT_TRAP_DOUBLE_WITH_QOS &&
                    c.suit == SUIT_SPADES)
                    score += 3.0f;
                /* Binding = forces consecutive trick wins */
                if (td->effect == TEFFECT_BINDING_AUTO_WIN_NEXT)
                    score += 2.0f;
            } else {
                /* Playing: apply beneficial transmutations */
                float base = rate_transmutation(td->id);
                score = base;

                /* Inversion on hearts = negate the points */
                if (td->effect == TEFFECT_INVERSION_NEGATE_POINTS &&
                    c.suit == SUIT_HEARTS)
                    score += 5.0f;

                /* Shield on any card = 3 tricks of immunity */
                if (td->effect == TEFFECT_WOTT_SHIELD_NEXT_TRICK)
                    score += 3.0f;

                /* Pendulum on hearts = -1 instead of +1 */
                if (td->effect == TEFFECT_WOTT_REDUCE_SCORE_1 &&
                    c.suit == SUIT_HEARTS)
                    score += 3.0f;

                /* Fog on high-value strategic cards */
                if (td->effect == TEFFECT_FOG_HIDDEN && c.rank >= RANK_Q)
                    score += 2.0f;

                /* Joker on low cards for control */
                if (td->effect == TEFFECT_JOKER_LEAD_WIN && c.rank <= RANK_5)
                    score += 3.0f;

                /* Bounty on spade cards near QS */
                if (td->effect == TEFFECT_BOUNTY_REDIRECT_QOS &&
                    c.suit == SUIT_SPADES)
                    score += 2.0f;
            }

            if (score > best_score) {
                best_score = score;
                best_slot = s;
                best_target = i;
            }
        }
    }

    if (best_slot >= 0 && best_target >= 0) {
        transmute_apply(hand, hts, inv, best_target,
                        inv->items[best_slot], player_id);
    }
}

/* ================================================================
 * Rogue Effect (Phase C4)
 * ================================================================ */

void comp_ai_rogue_pick(const CompetitiveAIState *ai,
                        const GameState *gs, int winner,
                        int *out_target, int *out_suit)
{
    *out_target = -1;
    *out_suit = -1;

    /* If QS is not played, target most likely holder and pick spades */
    if (ai->qs_status == QS_LOCATION_UNKNOWN ||
        ai->qs_status == QS_PASSED_TO) {
        int target = (ai->qs_status == QS_PASSED_TO)
                         ? ai->qs_related_player : -1;

        if (target < 0) {
            /* Find player not void in spades with most cards */
            int best = -1, best_count = 0;
            for (int p = 0; p < NUM_PLAYERS; p++) {
                if (p == winner) continue;
                if (ai->player_void[p][SUIT_SPADES]) continue;
                int cnt = gs->players[p].hand.count;
                if (cnt > best_count) {
                    best_count = cnt;
                    best = p;
                }
            }
            target = best;
        }

        if (target >= 0 && gs->players[target].hand.count > 0) {
            *out_target = target;
            *out_suit = SUIT_SPADES;
            return;
        }
    }

    /* Default: target player with most cards, pick suit they're not void in */
    int best_count = 0;
    for (int p = 0; p < NUM_PLAYERS; p++) {
        if (p == winner) continue;
        int cnt = gs->players[p].hand.count;
        if (cnt > best_count) {
            best_count = cnt;
            *out_target = p;
        }
    }
    if (*out_target >= 0 && gs->players[*out_target].hand.count > 0) {
        /* Pick a suit they're likely not void in */
        for (int s = SUIT_SPADES; s >= 0; s--) {
            if (!ai->player_void[*out_target][s]) {
                *out_suit = s;
                return;
            }
        }
        /* Fallback: random suit */
        *out_suit = rand() % SUIT_COUNT;
    }
}

/* ================================================================
 * Duel Effect (Phase C4)
 * ================================================================ */

void comp_ai_duel_pick(const CompetitiveAIState *ai,
                       const GameState *gs, const Phase2State *p2,
                       int winner,
                       int *out_target, int *out_target_idx,
                       int *out_give_idx)
{
    (void)p2;
    *out_target = -1;
    *out_target_idx = -1;
    *out_give_idx = -1;

    /* Find opponent with most cards */
    int best_p = -1, best_count = 0;
    for (int p = 0; p < NUM_PLAYERS; p++) {
        if (p == winner) continue;
        int cnt = gs->players[p].hand.count;
        if (cnt > best_count) {
            best_count = cnt;
            best_p = p;
        }
    }
    if (best_p < 0) return;

    *out_target = best_p;
    const Hand *their_hand = &gs->players[best_p].hand;
    const Hand *our_hand = &gs->players[winner].hand;

    if (their_hand->count == 0 || our_hand->count == 0) return;

    /* Take: their highest heart, or highest spade above Q, or random */
    int take_idx = 0;
    int take_priority = -1;
    for (int i = 0; i < their_hand->count; i++) {
        Card c = their_hand->cards[i];
        int pri = 0;
        if (is_qs(c)) pri = 100;
        else if (c.suit == SUIT_HEARTS) pri = 10 + c.rank;
        else if (c.suit == SUIT_SPADES && c.rank > RANK_Q) pri = 8 + c.rank;
        if (pri > take_priority) {
            take_priority = pri;
            take_idx = i;
        }
    }
    *out_target_idx = take_idx;

    /* Give: our most dangerous card */
    int give_idx = 0;
    int give_priority = -1;
    bool qs_live = (ai->qs_status != QS_PLAYED && ai->qs_status != QS_IN_MY_HAND);
    for (int i = 0; i < our_hand->count; i++) {
        Card c = our_hand->cards[i];
        int pri = 0;
        if (c.suit == SUIT_SPADES && c.rank == RANK_A && qs_live) pri = 20;
        else if (c.suit == SUIT_SPADES && c.rank == RANK_K && qs_live) pri = 19;
        else if (is_qs(c)) pri = 18;
        else if (c.suit == SUIT_HEARTS && c.rank == RANK_A) pri = 15;
        else if (c.suit == SUIT_HEARTS) pri = 10 + c.rank;
        else if (c.rank == RANK_A) pri = 5;
        else if (c.rank == RANK_K) pri = 4;
        if (pri > give_priority) {
            give_priority = pri;
            give_idx = i;
        }
    }
    *out_give_idx = give_idx;
}
