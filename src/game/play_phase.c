/* ============================================================
 * @deps-implements: play_phase.h
 * @deps-requires: play_phase.h, core/game_state.h (game_state_play_card),
 *                 core/hand.h, core/trick.h, render/render.h (mirror_source_tid),
 *                 phase2/phase2_state.h (curse_force_hearts[], anchor_force_suit[]),
 *                 phase2/transmutation_logic.h (transmute_curse_is_valid_lead, transmute_curse_consume,
 *                   transmute_anchor_is_valid_lead, transmute_anchor_consume),
 *                 phase2/transmutation.h (TrickTransmuteInfo.fogged/fog_transmuter),
 *                 phase2/phase2_defs.h (phase2_get_transmutation), stdio.h
 * @deps-last-changed: 2026-04-04 — Sets mirror_source_tid for Mirror transmutation detection
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
                              int player_id, Card card, int hint_idx)
{
    if (!p2->enabled) {
        bool ok = game_state_play_card(gs, player_id, card);
        if (ok) pps->card_played_sfx = true;
        return ok;
    }

    Hand *hand = &gs->players[player_id].hand;
    HandTransmuteState *hts = &p2->players[player_id].hand_transmutes;

    /* hint_idx carries transmutation_id (-1 = non-transmuted).
     * Match by suit+rank+transmutation state for order-independent disambiguation. */
    int hint_tid = hint_idx;  /* repurposed: transmutation_id, not position */
    int hand_idx = -1;
    for (int i = 0; i < hand->count; i++) {
        if (card_equals(hand->cards[i], card) &&
            hts->slots[i].transmutation_id == hint_tid) {
            hand_idx = i;
            break;
        }
    }
    /* Fallback: if exact tid match fails, take first suit+rank match */
    if (hand_idx < 0) {
        for (int i = 0; i < hand->count; i++) {
            if (card_equals(hand->cards[i], card)) {
                hand_idx = i;
                break;
            }
        }
    }


    if (hand_idx < 0) return false;  /* Card not in hand */

    /* Record transmutation ID into trick info */
    int trick_slot = gs->current_trick.num_played;
    int tid = -1;
    bool is_transmuted = transmute_is_transmuted(hts, hand_idx);
    if (is_transmuted) {
        tid = hts->slots[hand_idx].transmutation_id;
    }

    /* Curse: if leading and cursed, enforce hearts-only lead */
    bool leading = (gs->current_trick.num_played == 0);
    if (leading && p2->curse_force_hearts[player_id]) {
        if (!transmute_curse_is_valid_lead(hand, card)) return false;
    }

    /* Anchor: if leading and anchored (and not cursed), enforce suit lead */
    if (leading && !p2->curse_force_hearts[player_id] &&
        p2->anchor_force_suit[player_id] >= 0) {
        if (!transmute_anchor_is_valid_lead(
                hand, card, p2->anchor_force_suit[player_id]))
            return false;
    }

    if (gs->phase != PHASE_PLAYING) return false;
    if (game_state_current_player(gs) != player_id) return false;
    bool first_trick = (gs->tricks_played == 0);

    if (is_transmuted) {
        /* Curse bypasses hearts-broken restriction when leading */
        bool hb = gs->hearts_broken || (leading && p2->curse_force_hearts[player_id]);
        if (!transmute_is_valid_play(&gs->current_trick, hand, hts,
                                     hand_idx, card,
                                     hb, first_trick)) {
            return false;
        }
    } else {
        /* Curse bypasses hearts-broken for non-transmuted cards */
        bool was_broken = gs->hearts_broken;
        if (leading && p2->curse_force_hearts[player_id]) {
            gs->hearts_broken = true;
        }
        if (!trick_is_valid_play(&gs->current_trick, hand, card,
                                 gs->hearts_broken, first_trick)) {
            gs->hearts_broken = was_broken;
            return false;
        }
    }

    hand_remove_at(hand, hand_idx);
    trick_play_card(&gs->current_trick, player_id, card);
    if (card.suit == SUIT_HEARTS) {
        gs->hearts_broken = true;
    }

    /* Consume curse after leading */
    if (leading && p2->curse_force_hearts[player_id]) {
        transmute_curse_consume(p2, gs, player_id);
    }

    /* Consume anchor after leading */
    if (leading && p2->anchor_force_suit[player_id] >= 0) {
        transmute_anchor_consume(p2, player_id);
    }

    /* Set SFX flags */
    pps->card_played_sfx = true;
    if (is_transmuted) pps->transmute_sfx = true;

    /* Record in trick transmute info */
    if (trick_slot < CARDS_PER_TRICK) {
        pps->current_tti.transmutation_ids[trick_slot] = tid;
        pps->current_tti.transmuter_player[trick_slot] =
            (hand_idx >= 0) ? hts->slots[hand_idx].transmuter_player : -1;

        /* Record fog state */
        bool card_fogged = (hand_idx >= 0) && hts->slots[hand_idx].fogged;
        pps->current_tti.fogged[trick_slot] = card_fogged;
        pps->current_tti.fog_transmuter[trick_slot] =
            card_fogged ? hts->slots[hand_idx].fog_transmuter : -1;

        /* Resolve effect (handles Mirror chaining) and record */
        TransmuteEffect resolved = transmute_resolve_effect(tid, p2);
        pps->current_tti.resolved_effects[trick_slot] = resolved;

        /* Update Mirror animation state for this trick slot */
        if (tid >= 0) {
            const TransmutationDef *td2 = phase2_get_transmutation(tid);
            if (td2 && td2->effect == TEFFECT_MIRROR &&
                resolved != TEFFECT_FOG_HIDDEN && resolved != TEFFECT_NONE) {
                rs->mirror_source_tid[trick_slot] =
                    phase2_find_transmute_by_effect(p2->last_played_resolved_effect);
                rs->mirror_morphed[trick_slot] = false;
            } else {
                rs->mirror_source_tid[trick_slot] = -1;
            }
            const TransmutationDef *td_chk = phase2_get_transmutation(tid);
            if (!td_chk || td_chk->effect != TEFFECT_MIRROR)
                p2->last_played_transmuted_card = card;
            p2->last_played_transmute_id = tid;
            p2->last_played_resolved_effect = resolved;
        }

        /* Set render fog/transmute state immediately so sync_hands picks it up
         * on the same frame (info_sync_update may have already run this frame) */
        rs->trick_transmute_ids[trick_slot] = tid;
        if (card_fogged) {
            int fog_tp = hts->slots[hand_idx].fog_transmuter;
            rs->trick_fog_mode[trick_slot] = (fog_tp == 0) ? 1 : 2;
        } else if (resolved == TEFFECT_FOG_HIDDEN) {
            int ttp = (hand_idx >= 0) ? hts->slots[hand_idx].transmuter_player : -1;
            rs->trick_fog_mode[trick_slot] = (ttp == 0) ? 1 : 2;
        } else {
            rs->trick_fog_mode[trick_slot] = 0;
        }
    }

    /* Log transmuted card play in violet */
    if (is_transmuted && tid >= 0 && trick_slot < CARDS_PER_TRICK) {
        bool card_fogged = pps->current_tti.fogged[trick_slot];
        if (card_fogged) {
            /* Suppress effect details — only announce fog */
            render_chat_log_push_color(rs, "[Fog] A hidden card was played", VIOLET);
        } else {
            const TransmutationDef *tdef = phase2_get_transmutation(tid);
            if (tdef) {
                char tmsg[CHAT_MSG_LEN];
                snprintf(tmsg, sizeof(tmsg), "%s plays %s!",
                         p2_player_name(player_id), tdef->name);
                render_chat_log_push_rich(rs, tmsg, PURPLE,
                                          tdef->name, tid);
            }
        }
    }

    /* Sync hand transmute state (hand_remove_card shifted cards left) */
    if (hand_idx >= 0) {
        transmute_hand_remove_at(hts, hand_idx, hand->count);
    }

    return true;
}
