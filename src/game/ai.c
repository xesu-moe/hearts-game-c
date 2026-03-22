/* ============================================================
 * @deps-implements: ai.h
 * @deps-requires: ai.h, core/game_state.h (GameState), core/hand.h (Hand),
 *                 render/render.h (render_chat_log_push),
 *                 phase2/phase2_state.h (curse_force_hearts[], anchor_force_suit[]),
 *                 phase2/transmutation_logic.h (transmute_curse_is_valid_lead, transmute_anchor_is_valid_lead),
 *                 phase2/phase2_defs.h (p2_player_name), stdlib.h, stdio.h
 * @deps-last-changed: 2026-03-21 — Added Anchor transmutation: check force lead suit in valid play
 * ============================================================ */

#include "ai.h"

#include <stdbool.h>
#include <stdlib.h>

#include <stdio.h>

#include "core/hand.h"
#include "render/render.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

void ai_select_pass(GameState *gs, int player_id)
{
    Hand *hand = &gs->players[player_id].hand;
    int pc = gs->pass_card_count;
    Card pass_cards[MAX_PASS_CARD_COUNT];
    int pass_count = 0;

    /* Simple heuristic: pick cards with highest point value, then highest rank */
    bool used[MAX_HAND_SIZE] = {false};

    for (int p = 0; p < pc; p++) {
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

    if (pass_count == pc) {
        game_state_select_pass(gs, player_id, pass_cards, pc);
    }
}

void ai_play_card(GameState *gs, RenderState *rs, Phase2State *p2,
                  PlayPhaseState *pps, int player_id)
{
    const Hand *hand = &gs->players[player_id].hand;
    bool first_trick = (gs->tricks_played == 0);
    bool leading = (gs->current_trick.num_played == 0);
    bool cursed = p2->enabled && p2->curse_force_hearts[player_id];
    bool anchored = p2->enabled && p2->anchor_force_suit[player_id] >= 0;
    for (int i = 0; i < hand->count; i++) {
        bool valid;
        if (p2->enabled) {
            bool hb = gs->hearts_broken || (leading && cursed);
            valid = transmute_is_valid_play(
                &gs->current_trick, hand,
                &p2->players[player_id].hand_transmutes, i,
                hand->cards[i], hb, first_trick);
            if (leading && cursed && valid) {
                valid = transmute_curse_is_valid_lead(hand, hand->cards[i]);
            }
            if (leading && !cursed && anchored && valid) {
                valid = transmute_anchor_is_valid_lead(
                    hand, hand->cards[i],
                    p2->anchor_force_suit[player_id]);
            }
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

void ai_rogue_choose(const GameState *gs, int winner,
                     int *out_player, int *out_hand_idx)
{
    *out_player = -1;
    *out_hand_idx = -1;

    /* Pick opponent with most cards remaining (random tiebreak) */
    int best_count = 0;
    int candidates[NUM_PLAYERS];
    int num_candidates = 0;

    for (int p = 0; p < NUM_PLAYERS; p++) {
        if (p == winner) continue;
        int cnt = gs->players[p].hand.count;
        if (cnt <= 0) continue;
        if (cnt > best_count) {
            best_count = cnt;
            num_candidates = 0;
        }
        if (cnt == best_count) {
            candidates[num_candidates++] = p;
        }
    }

    if (num_candidates == 0) return;

    int chosen_p = candidates[rand() % num_candidates];
    int chosen_idx = rand() % gs->players[chosen_p].hand.count;

    *out_player = chosen_p;
    *out_hand_idx = chosen_idx;
}

void ai_duel_choose(const GameState *gs, int winner,
                    int *out_target_player, int *out_target_idx,
                    int *out_own_idx)
{
    *out_target_player = -1;
    *out_target_idx = -1;
    *out_own_idx = -1;

    /* Pick random opponent (most cards, random tiebreak — same as rogue) */
    int tp = -1, ti = -1;
    ai_rogue_choose(gs, winner, &tp, &ti);
    if (tp < 0 || ti < 0) return;

    /* Pick random card from own hand to give */
    int own_count = gs->players[winner].hand.count;
    if (own_count <= 0) return;

    *out_target_player = tp;
    *out_target_idx = ti;
    *out_own_idx = rand() % own_count;
}

void ai_duel_execute(GameState *gs, Phase2State *p2, RenderState *rs,
                     int winner)
{
    int out_p = -1, out_idx = -1, own_idx = -1;
    ai_duel_choose(gs, winner, &out_p, &out_idx, &own_idx);
    if (out_p < 0 || out_idx < 0 || own_idx < 0) return;

    transmute_swap_between_players(gs, p2, winner, own_idx, out_p, out_idx);

    char msg[CHAT_MSG_LEN];
    snprintf(msg, sizeof(msg), "Duel: %s swaps a card with %s!",
             p2_player_name(winner), p2_player_name(out_p));
    render_chat_log_push(rs, msg);
}
