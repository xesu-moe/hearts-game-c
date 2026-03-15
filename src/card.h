#ifndef CARD_H
#define CARD_H

/* ============================================================
 * @deps-exports: struct Card, enum Suit, enum Rank, card_to_index(), card_from_index(), card_is_none(), card_equals(), card_name(), card_points()
 * @deps-requires: (none — leaf type)
 * @deps-used-by: card.c, hand.h, deck.h, trick.h, card_render.h, render.h
 * @deps-last-changed: 2026-03-14 — Integrated into render system
 * ============================================================ */

#include <stdbool.h>

/* --- Constants --- */

#define DECK_SIZE       52
#define MAX_HAND_SIZE   13
#define NUM_PLAYERS     4
#define CARDS_PER_TRICK 4

/* --- Suits --- */

typedef enum Suit {
    SUIT_CLUBS    = 0,
    SUIT_DIAMONDS = 1,
    SUIT_SPADES   = 2,
    SUIT_HEARTS   = 3,
    SUIT_COUNT    = 4
} Suit;

/* --- Ranks (2 through Ace) --- */

typedef enum Rank {
    RANK_2  = 0,
    RANK_3  = 1,
    RANK_4  = 2,
    RANK_5  = 3,
    RANK_6  = 4,
    RANK_7  = 5,
    RANK_8  = 6,
    RANK_9  = 7,
    RANK_10 = 8,
    RANK_J  = 9,
    RANK_Q  = 10,
    RANK_K  = 11,
    RANK_A  = 12,
    RANK_COUNT = 13
} Rank;

/* --- Card --- */

typedef struct Card {
    Suit suit;
    Rank rank;
} Card;

/* Sentinel for "no card" / empty slot */
#define CARD_NONE ((Card){ .suit = -1, .rank = -1 })

/* Convert a card to a unique integer index (0-51). */
int card_to_index(Card card);

/* Convert an integer index (0-51) back to a Card. */
Card card_from_index(int index);

/* Check if a card is the CARD_NONE sentinel. */
bool card_is_none(Card card);

/* Check if two cards are equal (same suit and rank). */
bool card_equals(Card a, Card b);

/* Short string name for a card (e.g., "2C", "QS", "AH").
 * Returns a pointer to a static buffer — invalidated on next call. */
const char *card_name(Card card);

/* Point value in Hearts: each heart = 1, Queen of Spades = 13, others = 0. */
int card_points(Card card);

#endif /* CARD_H */
