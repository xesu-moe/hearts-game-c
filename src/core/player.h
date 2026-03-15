#ifndef PLAYER_H
#define PLAYER_H

/* ============================================================
 * @deps-exports: struct Player, player_init(), player_new_round(), player_add_to_total()
 * @deps-requires: hand.h (Hand)
 * @deps-used-by: game_state.h
 * @deps-last-changed: 2026-03-15 — Directory restructure
 * ============================================================ */

#include "hand.h"
#include <stdbool.h>

typedef struct Player {
    int  id;            /* player index 0-3 */
    Hand hand;          /* current hand of cards */
    int  round_points;  /* points taken this round */
    int  total_score;   /* cumulative score across all rounds */
    bool is_human;      /* true if controlled by the human */
} Player;

/* Initialize a player with the given id and human flag. */
void player_init(Player *player, int id, bool is_human);

/* Reset round_points to 0 and clear hand for a new round. */
void player_new_round(Player *player);

/* Add round_points to total_score. */
void player_add_to_total(Player *player);

#endif /* PLAYER_H */
