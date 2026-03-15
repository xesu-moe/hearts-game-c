#ifndef HAND_H
#define HAND_H

/* ============================================================
 * @deps-exports: struct Hand, hand_init(), hand_add_card(), hand_remove_at(), hand_remove_card(), hand_contains(), hand_has_suit(), hand_sort(), hand_count_points()
 * @deps-requires: card.h (Card, Suit, MAX_HAND_SIZE)
 * @deps-used-by: deck.h, player.h, trick.h
 * @deps-last-changed: 2026-03-15 — Directory restructure
 * ============================================================ */

#include "card.h"

typedef struct Hand {
    Card cards[MAX_HAND_SIZE];
    int  count;
} Hand;

/* Initialize a hand to empty. */
void hand_init(Hand *hand);

/* Add a card to the hand. Returns false if hand is full. */
bool hand_add_card(Hand *hand, Card card);

/* Remove and return the card at the given index (order-preserving).
 * Returns CARD_NONE if index is out of range. */
Card hand_remove_at(Hand *hand, int index);

/* Remove a specific card from the hand by suit and rank.
 * Returns true if found and removed. */
bool hand_remove_card(Hand *hand, Card card);

/* Check if the hand contains a specific card. */
bool hand_contains(const Hand *hand, Card card);

/* Check if the hand contains any card of the given suit. */
bool hand_has_suit(const Hand *hand, Suit suit);

/* Sort the hand by suit then rank. */
void hand_sort(Hand *hand);

/* Count total point value of cards in the hand. */
int hand_count_points(const Hand *hand);

#endif /* HAND_H */
