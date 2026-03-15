#ifndef DECK_H
#define DECK_H

/* ============================================================
 * @deps-exports: struct Deck, deck_init(), deck_shuffle(), deck_deal(), deck_deal_all(), deck_remaining()
 * @deps-requires: card.h (Card, DECK_SIZE), hand.h (Hand, NUM_PLAYERS)
 * @deps-used-by: game_state.h
 * @deps-last-changed: 2026-03-15 — Directory restructure
 * ============================================================ */

#include "card.h"
#include "hand.h"

typedef struct Deck {
    Card cards[DECK_SIZE];
    int  top; /* index of the next card to deal (decrements toward 0) */
} Deck;

/* Initialize the deck with all 52 cards in order. Sets top to 51. */
void deck_init(Deck *deck);

/* Shuffle the deck using Fisher-Yates. Uses Raylib's GetRandomValue(). */
void deck_shuffle(Deck *deck);

/* Deal one card from the top of the deck. Returns CARD_NONE if empty. */
Card deck_deal(Deck *deck);

/* Deal all 52 cards evenly to 4 hands (13 each). Hands must be initialized. */
void deck_deal_all(Deck *deck, Hand hands[NUM_PLAYERS]);

/* Return how many cards remain in the deck. */
int deck_remaining(const Deck *deck);

#endif /* DECK_H */
