/* ============================================================
 * @deps-implements: deck.h
 * @deps-requires: deck.h (Deck, Hand, card_from_index, hand_add_card, DECK_SIZE, NUM_PLAYERS, CARD_NONE), raylib.h (GetRandomValue)
 * @deps-last-changed: 2026-03-13 — Initial creation
 * ============================================================ */

#include "deck.h"
#include "raylib.h"

void deck_init(Deck *deck)
{
    for (int i = 0; i < DECK_SIZE; i++) {
        deck->cards[i] = card_from_index(i);
    }
    deck->top = DECK_SIZE - 1;
}

void deck_shuffle(Deck *deck)
{
    for (int i = DECK_SIZE - 1; i > 0; i--) {
        int j = GetRandomValue(0, i);
        Card temp = deck->cards[i];
        deck->cards[i] = deck->cards[j];
        deck->cards[j] = temp;
    }
    deck->top = DECK_SIZE - 1;
}

Card deck_deal(Deck *deck)
{
    if (deck->top < 0) {
        return CARD_NONE;
    }
    Card card = deck->cards[deck->top];
    deck->top--;
    return card;
}

void deck_deal_all(Deck *deck, Hand hands[NUM_PLAYERS])
{
    for (int i = 0; i < DECK_SIZE; i++) {
        hand_add_card(&hands[i % NUM_PLAYERS], deck_deal(deck));
    }
}

int deck_remaining(const Deck *deck)
{
    return deck->top + 1;
}
