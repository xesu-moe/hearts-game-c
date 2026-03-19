/* ============================================================
 * @deps-implements: ai.h
 * @deps-requires: ai.h, core/game_state.h, core/hand.h, core/card.h,
 *                 render/render.h, phase2/transmutation_logic.h,
 *                 game/play_phase.h
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "ai.h"

#include <stdbool.h>

#include "core/hand.h"
#include "render/render.h"
#include "phase2/transmutation_logic.h"

void ai_select_pass(GameState *gs, int player_id)
{
    Hand *hand = &gs->players[player_id].hand;
    Card pass_cards[PASS_CARD_COUNT];
    int pass_count = 0;

    /* Simple heuristic: pick cards with highest point value, then highest rank */
    bool used[MAX_HAND_SIZE] = {false};

    for (int p = 0; p < PASS_CARD_COUNT; p++) {
        int best = -1;
        int best_points = -1;
        int best_rank = -1;
        for (int i = 0; i < hand->count; i++) {
            if (used[i]) continue;
            int pts = card_points(hand->cards[i]);
            int rnk = hand->cards[i].rank;
            if (pts > best_points || (pts == best_points && rnk > best_rank)) {
                best = i;
                best_points = pts;
                best_rank = rnk;
            }
        }
        if (best >= 0) {
            pass_cards[pass_count++] = hand->cards[best];
            used[best] = true;
        }
    }

    if (pass_count == PASS_CARD_COUNT) {
        game_state_select_pass(gs, player_id, pass_cards);
    }
}

void ai_play_card(GameState *gs, RenderState *rs, Phase2State *p2,
                  PlayPhaseState *pps, int player_id)
{
    const Hand *hand = &gs->players[player_id].hand;
    bool first_trick = (gs->tricks_played == 0);
    for (int i = 0; i < hand->count; i++) {
        bool valid;
        if (p2->enabled) {
            valid = transmute_is_valid_play(
                &gs->current_trick, hand,
                &p2->players[player_id].hand_transmutes, i,
                hand->cards[i], gs->hearts_broken, first_trick);
        } else {
            valid = game_state_is_valid_play(gs, player_id, hand->cards[i]);
        }
        if (valid) {
            play_card_with_transmute(gs, rs, p2, pps, player_id,
                                     hand->cards[i]);
            return;
        }
    }
}
