/* ============================================================
 * @deps-implements: player.h
 * @deps-requires: player.h (Player, Hand, hand_init)
 * @deps-last-changed: 2026-03-15 — Directory restructure
 * ============================================================ */

#include "player.h"

void player_init(Player *player, int id, bool is_human)
{
    player->id = id;
    player->is_human = is_human;
    player->round_points = 0;
    player->total_score = 0;
    hand_init(&player->hand);
}

void player_new_round(Player *player)
{
    player->round_points = 0;
    hand_init(&player->hand);
}

void player_add_to_total(Player *player)
{
    player->total_score += player->round_points;
}
