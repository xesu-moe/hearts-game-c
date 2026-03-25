/* ============================================================
 * @deps-implements: game_state.h
 * @deps-requires: game_state.h (PHASE_DEALING, PHASE_SCORING), player.h, deck.h, trick.h,
 *                 hand.h, card.h
 * @deps-last-changed: 2026-03-20 — Added game_state_complete_trick_with() implementation
 * ============================================================ */

#include "game_state.h"
#include <string.h>

#define TRICKS_PER_ROUND 13
#define ALL_POINTS       26

void game_state_init(GameState *gs)
{
    memset(gs, 0, sizeof(*gs));
    for (int i = 0; i < NUM_PLAYERS; i++) {
        player_init(&gs->players[i], i, i == 0);
    }
    gs->phase = PHASE_LOGIN;
    gs->round_number = 0;
}

bool game_state_start_game(GameState *gs)
{
    if (gs->phase != PHASE_MENU && gs->phase != PHASE_GAME_OVER) {
        return false;
    }

    for (int i = 0; i < NUM_PLAYERS; i++) {
        gs->players[i].total_score = 0;
    }
    gs->round_number = 0;

    game_state_new_round(gs);

    return true;
}

void game_state_reset_to_menu(GameState *gs)
{
    game_state_init(gs);
}

int game_state_find_two_of_clubs(const GameState *gs)
{
    Card two_clubs = {SUIT_CLUBS, RANK_2};
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (hand_contains(&gs->players[i].hand, two_clubs)) {
            return i;
        }
    }
    return -1;
}

void game_state_new_round(GameState *gs)
{
    for (int i = 0; i < NUM_PLAYERS; i++) {
        player_new_round(&gs->players[i]);
    }

    deck_init(&gs->deck);
    deck_shuffle(&gs->deck);

    /* Deal card-by-card to avoid stride mismatch with deck_deal_all */
    for (int i = 0; i < DECK_SIZE; i++) {
        hand_add_card(&gs->players[i % NUM_PLAYERS].hand, deck_deal(&gs->deck));
    }

    for (int i = 0; i < NUM_PLAYERS; i++) {
        hand_sort(&gs->players[i].hand);
    }

    gs->pass_direction = PASS_LEFT;  /* default, dealer overrides later */
    gs->pass_card_count = DEFAULT_PASS_CARD_COUNT;
    gs->hearts_broken = false;
    gs->tricks_played = 0;

    /* Always clear trick to prevent stale cards from previous round */
    trick_init(&gs->current_trick, -1);

    if (gs->pass_direction == PASS_NONE) {
        gs->lead_player = game_state_find_two_of_clubs(gs);
        trick_init(&gs->current_trick, gs->lead_player);
    } else {
        /* Lead player determined after passing (2♣ may move) */
        gs->lead_player = -1;
    }

    /* Clear pass state */
    gs->skip_human_pass_sort = false;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        for (int j = 0; j < MAX_PASS_CARD_COUNT; j++) {
            gs->pass_selections[i][j] = CARD_NONE;
        }
        gs->pass_ready[i] = false;
    }

    gs->phase = PHASE_DEALING;
    gs->round_number++;
}

bool game_state_select_pass(GameState *gs, int player_id,
                            const Card cards[], int card_count)
{
    if (gs->phase != PHASE_PASSING) {
        return false;
    }
    if (player_id < 0 || player_id >= NUM_PLAYERS) {
        return false;
    }

    const Hand *hand = &gs->players[player_id].hand;

    /* Verify all cards are in hand */
    for (int i = 0; i < card_count; i++) {
        if (!hand_contains(hand, cards[i])) {
            return false;
        }
    }

    /* Check no duplicates */
    for (int i = 0; i < card_count; i++) {
        for (int j = i + 1; j < card_count; j++) {
            if (card_equals(cards[i], cards[j])) {
                return false;
            }
        }
    }

    /* Store selection */
    for (int i = 0; i < card_count; i++) {
        gs->pass_selections[player_id][i] = cards[i];
    }
    gs->pass_ready[player_id] = true;

    return true;
}

bool game_state_all_passes_ready(const GameState *gs)
{
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (!gs->pass_ready[i]) {
            return false;
        }
    }
    return true;
}

bool game_state_execute_pass(GameState *gs)
{
    if (!game_state_all_passes_ready(gs)) {
        return false;
    }

    static const int offsets[PASS_COUNT] = {1, 3, 2, 0};
    _Static_assert(PASS_COUNT == 4, "Update offsets array if PassDirection changes");
    int offset = offsets[gs->pass_direction];

    int pc = gs->pass_card_count;

    /* Phase 1: Remove selected cards from each player's hand */
    Card outgoing[NUM_PLAYERS][MAX_PASS_CARD_COUNT];
    for (int i = 0; i < NUM_PLAYERS; i++) {
        for (int j = 0; j < pc; j++) {
            outgoing[i][j] = gs->pass_selections[i][j];
            hand_remove_card(&gs->players[i].hand, outgoing[i][j]);
        }
    }

    /* Phase 2: Add cards to target players */
    for (int i = 0; i < NUM_PLAYERS; i++) {
        int target = (i + offset) % NUM_PLAYERS;
        for (int j = 0; j < pc; j++) {
            hand_add_card(&gs->players[target].hand, outgoing[i][j]);
        }
    }

    /* Re-sort hands and clear pass state */
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (!(i == 0 && gs->skip_human_pass_sort))
            hand_sort(&gs->players[i].hand);
        gs->pass_ready[i] = false;
        for (int j = 0; j < MAX_PASS_CARD_COUNT; j++) {
            gs->pass_selections[i][j] = CARD_NONE;
        }
    }

    /* Re-find 2♣ holder (may have moved during pass) */
    gs->lead_player = game_state_find_two_of_clubs(gs);
    trick_init(&gs->current_trick, gs->lead_player);

    /* Caller owns the phase transition (animation may need to run first) */
    return true;
}

int game_state_current_player(const GameState *gs)
{
    if (gs->phase != PHASE_PLAYING || trick_is_complete(&gs->current_trick)) {
        return -1;
    }
    return (gs->lead_player + gs->current_trick.num_played) % NUM_PLAYERS;
}

bool game_state_play_card(GameState *gs, int player_id, Card card)
{
    if (gs->phase != PHASE_PLAYING) {
        return false;
    }
    if (game_state_current_player(gs) != player_id) {
        return false;
    }

    Hand *hand = &gs->players[player_id].hand;
    bool first_trick = (gs->tricks_played == 0);

    if (!trick_is_valid_play(&gs->current_trick, hand, card,
                             gs->hearts_broken, first_trick)) {
        return false;
    }

    hand_remove_card(hand, card);
    trick_play_card(&gs->current_trick, player_id, card);

    /* Safe to update mid-trick: hearts_broken is only checked when leading,
     * and no player leads during a trick in progress. */
    if (card.suit == SUIT_HEARTS) {
        gs->hearts_broken = true;
    }

    return true;
}

bool game_state_complete_trick_with(GameState *gs, int winner, int points)
{
    if (gs->phase != PHASE_PLAYING || !trick_is_complete(&gs->current_trick)) {
        return false;
    }
    if (winner < 0 || winner >= NUM_PLAYERS) return false;

    gs->players[winner].round_points += points;
    gs->tricks_played++;

    if (gs->tricks_played >= TRICKS_PER_ROUND) {
        /* Shoot-the-moon check.
         * NOTE: Inversion (Phase 2) negates point cards per-trick, making
         * it effectively impossible to reach exactly 26. This is by design —
         * Inversion blocks moon-shooting as a natural consequence. */
        for (int i = 0; i < NUM_PLAYERS; i++) {
            if (gs->players[i].round_points == ALL_POINTS) {
                gs->players[i].round_points = 0;
                for (int j = 0; j < NUM_PLAYERS; j++) {
                    if (j != i) {
                        gs->players[j].round_points = ALL_POINTS;
                    }
                }
                break;
            }
        }

        for (int i = 0; i < NUM_PLAYERS; i++) {
            player_add_to_total(&gs->players[i]);
        }
        gs->phase = PHASE_SCORING;
    } else {
        gs->lead_player = winner;
        trick_init(&gs->current_trick, winner);
    }

    return true;
}

bool game_state_complete_trick(GameState *gs)
{
    int winner = trick_get_winner(&gs->current_trick);
    int points = trick_count_points(&gs->current_trick);
    return game_state_complete_trick_with(gs, winner, points);
}

bool game_state_is_valid_play(const GameState *gs, int player_id, Card card)
{
    if (gs->phase != PHASE_PLAYING) {
        return false;
    }
    if (game_state_current_player(gs) != player_id) {
        return false;
    }
    return trick_is_valid_play(&gs->current_trick, &gs->players[player_id].hand,
                               card, gs->hearts_broken,
                               gs->tricks_played == 0);
}

bool game_state_is_game_over(const GameState *gs)
{
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (gs->players[i].total_score >= GAME_OVER_SCORE) {
            return true;
        }
    }
    return false;
}

bool game_state_advance_scoring(GameState *gs)
{
    if (gs->phase != PHASE_SCORING) {
        return false;
    }

    if (game_state_is_game_over(gs)) {
        gs->phase = PHASE_GAME_OVER;
    } else {
        game_state_new_round(gs);
    }

    return true;
}

int game_state_get_winners(const GameState *gs, int winners[NUM_PLAYERS])
{
    if (gs->phase != PHASE_GAME_OVER) {
        return 0;
    }

    int lowest = gs->players[0].total_score;
    for (int i = 1; i < NUM_PLAYERS; i++) {
        if (gs->players[i].total_score < lowest) {
            lowest = gs->players[i].total_score;
        }
    }

    int count = 0;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (gs->players[i].total_score == lowest) {
            winners[count++] = i;
        }
    }

    return count;
}
