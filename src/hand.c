/* ============================================================
 * @deps-implements: hand.h
 * @deps-requires: hand.h (Hand, Card, card_equals, card_to_index, card_points, CARD_NONE)
 * @deps-last-changed: 2026-03-13 — Initial creation
 * ============================================================ */

#include "hand.h"

void hand_init(Hand *hand)
{
    *hand = (Hand){0};
}

bool hand_add_card(Hand *hand, Card card)
{
    if (hand->count >= MAX_HAND_SIZE) {
        return false;
    }
    hand->cards[hand->count] = card;
    hand->count++;
    return true;
}

Card hand_remove_at(Hand *hand, int index)
{
    if (index < 0 || index >= hand->count) {
        return CARD_NONE;
    }
    Card removed = hand->cards[index];
    hand->count--;
    /* Shift remaining cards down to preserve sort order */
    for (int i = index; i < hand->count; i++) {
        hand->cards[i] = hand->cards[i + 1];
    }
    return removed;
}

bool hand_remove_card(Hand *hand, Card card)
{
    for (int i = 0; i < hand->count; i++) {
        if (card_equals(hand->cards[i], card)) {
            hand_remove_at(hand, i);
            return true;
        }
    }
    return false;
}

bool hand_contains(const Hand *hand, Card card)
{
    for (int i = 0; i < hand->count; i++) {
        if (card_equals(hand->cards[i], card)) {
            return true;
        }
    }
    return false;
}

bool hand_has_suit(const Hand *hand, Suit suit)
{
    for (int i = 0; i < hand->count; i++) {
        if (hand->cards[i].suit == suit) {
            return true;
        }
    }
    return false;
}

void hand_sort(Hand *hand)
{
    /* Insertion sort — optimal for n <= 13 */
    for (int i = 1; i < hand->count; i++) {
        Card key = hand->cards[i];
        int key_val = card_to_index(key);
        int j = i - 1;
        while (j >= 0 && card_to_index(hand->cards[j]) > key_val) {
            hand->cards[j + 1] = hand->cards[j];
            j--;
        }
        hand->cards[j + 1] = key;
    }
}

int hand_count_points(const Hand *hand)
{
    int points = 0;
    for (int i = 0; i < hand->count; i++) {
        points += card_points(hand->cards[i]);
    }
    return points;
}
