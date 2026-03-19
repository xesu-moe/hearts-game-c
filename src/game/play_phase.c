/* ============================================================
 * @deps-implements: play_phase.h
 * @deps-requires: play_phase.h, core/game_state.h, core/hand.h, core/trick.h,
 *                 render/render.h, phase2/phase2_state.h,
 *                 phase2/transmutation_logic.h, phase2/phase2_defs.h
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "play_phase.h"

#include <stdio.h>

#include "core/hand.h"
#include "core/trick.h"
#include "render/render.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

const char *p2_player_name(int player_id)
{
    static const char *names[] = {"You", "West", "North", "East"};
    if (player_id >= 0 && player_id < NUM_PLAYERS) return names[player_id];
    return "???";
}

bool play_card_with_transmute(GameState *gs, RenderState *rs,
                              Phase2State *p2, PlayPhaseState *pps,
                              int player_id, Card card)
{
    if (!p2->enabled) {
        return game_state_play_card(gs, player_id, card);
    }

    Hand *hand = &gs->players[player_id].hand;
    HandTransmuteState *hts = &p2->players[player_id].hand_transmutes;

    /* Find the card's hand index before it's removed */
    int hand_idx = -1;
    for (int i = 0; i < hand->count; i++) {
        if (card_equals(hand->cards[i], card)) {
            hand_idx = i;
            break;
        }
    }

    /* Record transmutation ID into trick info */
    int trick_slot = gs->current_trick.num_played;
    int tid = -1;
    bool is_transmuted = (hand_idx >= 0 &&
                          transmute_is_transmuted(hts, hand_idx));
    if (is_transmuted) {
        tid = hts->slots[hand_idx].transmutation_id;
    }

    bool ok;
    if (is_transmuted) {
        if (gs->phase != PHASE_PLAYING) return false;
        if (game_state_current_player(gs) != player_id) return false;
        bool first_trick = (gs->tricks_played == 0);
        if (!transmute_is_valid_play(&gs->current_trick, hand, hts,
                                     hand_idx, card,
                                     gs->hearts_broken, first_trick)) {
            return false;
        }
        if (!hand_remove_card(hand, card)) return false;
        trick_play_card(&gs->current_trick, player_id, card);
        if (card.suit == SUIT_HEARTS) {
            gs->hearts_broken = true;
        }
        ok = true;
    } else {
        ok = game_state_play_card(gs, player_id, card);
        if (!ok) return false;
    }

    /* Record in trick transmute info */
    if (trick_slot < CARDS_PER_TRICK) {
        pps->current_tti.transmutation_ids[trick_slot] = tid;
    }

    /* Log transmuted card play in violet */
    if (is_transmuted && tid >= 0) {
        const TransmutationDef *tdef = phase2_get_transmutation(tid);
        if (tdef) {
            char tmsg[CHAT_MSG_LEN];
            snprintf(tmsg, sizeof(tmsg), "[%s] %s",
                     tdef->name, tdef->description);
            render_chat_log_push_color(rs, tmsg, VIOLET);
        }
    }

    /* Sync hand transmute state (hand_remove_card shifted cards left) */
    if (hand_idx >= 0) {
        transmute_hand_remove_at(hts, hand_idx, hand->count);
    }

    return true;
}
