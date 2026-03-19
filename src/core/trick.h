#ifndef TRICK_H
#define TRICK_H

/* ============================================================
 * @deps-exports: struct Trick, trick_init(), trick_play_card(), trick_is_complete(), trick_get_winner(), trick_count_points(), trick_is_valid_play()
 * @deps-requires: card.h (Card, Suit, CARDS_PER_TRICK), hand.h (Hand)
 * @deps-used-by: trick.c, game_state.h, contract_logic.h, play_phase.c, turn_flow.c, ai.c
 * @deps-last-changed: 2026-03-19 — Extended used_by: play_phase, turn_flow, ai modules
 * ============================================================ */

#include "card.h"
#include "hand.h"

typedef struct Trick {
    Card cards[CARDS_PER_TRICK];      /* cards played, indexed by play order */
    int  player_ids[CARDS_PER_TRICK]; /* which player played each card */
    int  lead_player;                 /* player who led this trick */
    Suit lead_suit;                   /* suit of the lead card */
    int  num_played;                  /* how many cards played so far (0-4) */
} Trick;

/* Initialize a trick to empty with the given lead player. */
void trick_init(Trick *trick, int lead_player);

/* Play a card into the trick. Sets lead_suit on the first card.
 * Returns false if the trick is already full. */
bool trick_play_card(Trick *trick, int player_id, Card card);

/* Check if the trick is complete (4 cards played). */
bool trick_is_complete(const Trick *trick);

/* Determine which player won the trick (highest card of lead suit).
 * Returns -1 if trick is not complete. */
int trick_get_winner(const Trick *trick);

/* Count total points in this trick. */
int trick_count_points(const Trick *trick);

/* Check if playing a card is legal per Hearts rules.
 * hearts_broken: whether a heart has been played in a previous trick this round.
 * first_trick: whether this is the first trick of the round. */
bool trick_is_valid_play(const Trick *trick, const Hand *hand, Card card,
                         bool hearts_broken, bool first_trick);

#endif /* TRICK_H */
