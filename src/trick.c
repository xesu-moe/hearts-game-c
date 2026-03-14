/* ============================================================
 * @deps-implements: trick.h
 * @deps-requires: trick.h (Trick, Card, Suit, CARDS_PER_TRICK, card_points, Hand, hand_contains, hand_has_suit)
 * @deps-last-changed: 2026-03-14 — Added trick_is_valid_play() implementation
 * ============================================================ */

#include "trick.h"

void trick_init(Trick *trick, int lead_player)
{
    *trick = (Trick){0};
    trick->lead_player = lead_player;
}

bool trick_play_card(Trick *trick, int player_id, Card card)
{
    if (trick->num_played >= CARDS_PER_TRICK) {
        return false;
    }

    int idx = trick->num_played;
    trick->cards[idx] = card;
    trick->player_ids[idx] = player_id;

    if (idx == 0) {
        trick->lead_suit = card.suit;
    }

    trick->num_played++;
    return true;
}

bool trick_is_complete(const Trick *trick)
{
    return trick->num_played == CARDS_PER_TRICK;
}

int trick_get_winner(const Trick *trick)
{
    if (!trick_is_complete(trick)) {
        return -1;
    }

    /* Card 0 is always the lead and always matches lead_suit */
    int best_idx = 0;
    Rank best_rank = trick->cards[0].rank;

    for (int i = 1; i < CARDS_PER_TRICK; i++) {
        if (trick->cards[i].suit == trick->lead_suit &&
            trick->cards[i].rank > best_rank) {
            best_idx = i;
            best_rank = trick->cards[i].rank;
        }
    }

    return trick->player_ids[best_idx];
}

int trick_count_points(const Trick *trick)
{
    int points = 0;
    for (int i = 0; i < trick->num_played; i++) {
        points += card_points(trick->cards[i]);
    }
    return points;
}

/* Check if every card in hand is the given suit. */
static bool hand_has_only_suit(const Hand *hand, Suit suit)
{
    if (hand->count == 0) {
        return false;
    }
    for (int i = 0; i < hand->count; i++) {
        if (hand->cards[i].suit != suit) {
            return false;
        }
    }
    return true;
}

bool trick_is_valid_play(const Trick *trick, const Hand *hand, Card card,
                         bool hearts_broken, bool first_trick)
{
    /* 1. Player must have the card */
    if (!hand_contains(hand, card)) {
        return false;
    }

    bool leading = (trick->num_played == 0);
    Card two_of_clubs = {SUIT_CLUBS, RANK_2};

    if (first_trick) {
        if (leading) {
            /* 2. First trick, leading — must play 2 of Clubs */
            return card_equals(card, two_of_clubs);
        }
        /* 3. First trick, following */
        if (hand_has_suit(hand, trick->lead_suit)) {
            return card.suit == trick->lead_suit;
        }
        /* Can't follow suit — cannot play point cards unless hand is all points */
        if (card_points(card) > 0) {
            /* Check if every card in hand has points */
            for (int i = 0; i < hand->count; i++) {
                if (card_points(hand->cards[i]) == 0) {
                    return false;
                }
            }
        }
        return true;
    }

    /* Non-first trick */
    if (leading) {
        /* 4. Can't lead hearts unless broken or hand is all hearts */
        if (card.suit == SUIT_HEARTS && !hearts_broken) {
            return hand_has_only_suit(hand, SUIT_HEARTS);
        }
        return true;
    }

    /* 5. Following — must follow suit if possible */
    if (hand_has_suit(hand, trick->lead_suit)) {
        return card.suit == trick->lead_suit;
    }
    return true;
}
