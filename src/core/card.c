/* ============================================================
 * @deps-implements: card.h
 * @deps-requires: card.h (Card, Suit, Rank, RANK_COUNT, CARD_NONE)
 * @deps-last-changed: 2026-03-15 — Directory restructure
 * ============================================================ */

#include "card.h"

int card_to_index(Card card)
{
    return card.suit * RANK_COUNT + card.rank;
}

Card card_from_index(int index)
{
    Card card;
    card.suit = (Suit)(index / RANK_COUNT);
    card.rank = (Rank)(index % RANK_COUNT);
    return card;
}

bool card_is_none(Card card)
{
    return card.suit < 0 || card.rank < 0;
}

bool card_equals(Card a, Card b)
{
    return a.suit == b.suit && a.rank == b.rank;
}

const char *card_name(Card card)
{
    static char buf[4];
    const char *rank_chars = "23456789TJQKA";
    const char *suit_chars = "CDSH";

    if (card_is_none(card)) {
        buf[0] = '?';
        buf[1] = '?';
        buf[2] = '\0';
        return buf;
    }

    buf[0] = rank_chars[card.rank];
    buf[1] = suit_chars[card.suit];
    buf[2] = '\0';
    return buf;
}

int card_points(Card card)
{
    if (card.suit == SUIT_HEARTS) {
        return 1;
    }
    if (card.suit == SUIT_SPADES && card.rank == RANK_Q) {
        return 13;
    }
    return 0;
}
