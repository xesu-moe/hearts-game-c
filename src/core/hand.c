/* ============================================================
 * @deps-implements: hand.h
 * @deps-requires: hand.h (hand_move_card), card.h (Card, card_equals, card_to_index, card_points, CARD_NONE, MAX_HAND_SIZE)
 * @deps-last-changed: 2026-03-19 — Added hand_move_card() implementation
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

void hand_sort_permutation(Hand *hand, int *perm)
{
    /* Initialize permutation as identity */
    for (int i = 0; i < hand->count; i++) {
        perm[i] = i;
    }

    /* Insertion sort — same as hand_sort, but also track perm */
    for (int i = 1; i < hand->count; i++) {
        Card key = hand->cards[i];
        int key_val = card_to_index(key);
        int key_perm = perm[i];
        int j = i - 1;
        while (j >= 0 && card_to_index(hand->cards[j]) > key_val) {
            hand->cards[j + 1] = hand->cards[j];
            perm[j + 1] = perm[j];
            j--;
        }
        hand->cards[j + 1] = key;
        perm[j + 1] = key_perm;
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

void hand_move_card(Hand *hand, int src_index, int dst_index)
{
    if (src_index < 0 || src_index >= hand->count) return;
    if (dst_index < 0 || dst_index >= hand->count) return;
    if (src_index == dst_index) return;

    Card card = hand->cards[src_index];
    if (src_index < dst_index) {
        for (int i = src_index; i < dst_index; i++)
            hand->cards[i] = hand->cards[i + 1];
    } else {
        for (int i = src_index; i > dst_index; i--)
            hand->cards[i] = hand->cards[i - 1];
    }
    hand->cards[dst_index] = card;
}
