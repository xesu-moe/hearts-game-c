/* ============================================================
 * @deps-implements: info_sync.h
 * @deps-requires: info_sync.h, core/game_state.h, render/render.h (mirror_source_tid),
 *                 phase2/phase2_state.h (PlayerPhase2.contracts[],
 *                   PlayerPhase2.num_active_contracts, curse_force_hearts[],
 *                   anchor_force_suit[]),
 *                 phase2/phase2_defs.h (phase2_get_contract, phase2_get_transmutation,
 *                   phase2_find_transmute_by_effect),
 *                 phase2/transmutation_logic.h, phase2/transmutation.h, stdio.h
 * @deps-last-changed: 2026-04-04 — Uses phase2_find_transmute_by_effect() and mirror_source_tid for Mirror transmutation detection
 * ============================================================ */

#include "info_sync.h"

#include <stdio.h>

#include "render/render.h"
#include "phase2/phase2_defs.h"
#include "phase2/transmutation_logic.h"

void info_sync_update(GameState *gs, RenderState *rs, Phase2State *p2,
                      PlayPhaseState *pls)
{
    if (p2->enabled) {
        /* Contracts (up to 3) */
        rs->info_contract_count = 0;
        for (int i = 0; i < 3; i++)
            rs->info_contract_transmute_id[i] = -1;
        for (int c = 0; c < p2->players[0].num_active_contracts && c < 3; c++) {
            if (p2->players[0].contracts[c].contract_id >= 0) {
                const ContractInstance *ci = &p2->players[0].contracts[c];
                const ContractDef *cd = phase2_get_contract(ci->contract_id);
                const TransmutationDef *td = (ci->paired_transmutation_id >= 0)
                    ? phase2_get_transmutation(ci->paired_transmutation_id) : NULL;
                if (cd) {
                    int idx = rs->info_contract_count;
                    snprintf(rs->info_contract_name[idx],
                             sizeof(rs->info_contract_name[idx]), "%s",
                             td ? td->name : cd->name);
                    snprintf(rs->info_contract_desc[idx],
                             sizeof(rs->info_contract_desc[idx]), "%s",
                             cd->description);
                    rs->info_contract_transmute_id[idx] =
                        ci->paired_transmutation_id;
                    rs->info_contract_count++;
                }
            }
        }

        /* Shield state */
        for (int i = 0; i < NUM_PLAYERS; i++) {
            rs->shield_remaining[i] = p2->shield_tricks_remaining[i];
        }

        /* Transmutation inventory + hand flags */
        {
            TransmuteInventory *inv = &p2->players[0].transmute_inv;
            int tcount = 0;
            for (int i = 0; i < inv->count && tcount < MAX_TRANSMUTE_BTNS; i++) {
                const TransmutationDef *td =
                    phase2_get_transmutation(inv->items[i]);
                if (td) {
                    rs->transmute_btn_ids[tcount] = inv->items[i];
                    tcount++;
                }
            }
            rs->transmute_btn_count = tcount;
            rs->pending_transmutation_id = pls->pending_transmutation;

            /* Hand transmute IDs + fog mode for player 0 */
            HandTransmuteState *hts = &p2->players[0].hand_transmutes;
            for (int i = 0; i < MAX_HAND_SIZE; i++) {
                /* Fog doesn't replace the card sprite — fog_mode overlay handles it */
                if (i < gs->players[0].hand.count &&
                    hts->slots[i].transmutation_id >= 0) {
                    const TransmutationDef *htd =
                        phase2_get_transmutation(hts->slots[i].transmutation_id);
                    int new_val = (htd && htd->effect == TEFFECT_FOG_HIDDEN) ? -1
                        : hts->slots[i].transmutation_id;
                    rs->hand_transmute_ids[i] = new_val;
                } else {
                    rs->hand_transmute_ids[i] = -1;
                }
                if (i < gs->players[0].hand.count &&
                    transmute_is_effective_fog(hts, i, p2)) {
                    int tp = (hts->slots[i].fogged)
                        ? hts->slots[i].fog_transmuter
                        : transmute_get_transmuter(hts, i);
                    rs->hand_fog_mode[i] = (tp == 0) ? 1 : 2;
                } else {
                    rs->hand_fog_mode[i] = 0;
                }
            }

            /* Trick transmute IDs + fog mode
             * Fog doesn't replace the trick card sprite — fog_mode overlay handles it */
            for (int i = 0; i < CARDS_PER_TRICK; i++) {
                {
                    int raw_tid = pls->current_tti.transmutation_ids[i];
                    const TransmutationDef *ttd =
                        (raw_tid >= 0) ? phase2_get_transmutation(raw_tid) : NULL;

                    /* Mirror source: reverse-lookup from resolved effect */
                    if (!rs->mirror_morphed[i] && ttd &&
                        ttd->effect == TEFFECT_MIRROR) {
                        TransmuteEffect eff = pls->current_tti.resolved_effects[i];
                        if (eff != TEFFECT_NONE && eff != TEFFECT_FOG_HIDDEN)
                            rs->mirror_source_tid[i] = phase2_find_transmute_by_effect(eff);
                    }

                    /* After morph: write morphed ID so render keeps the new sprite */
                    int display_tid = raw_tid;
                    if (rs->mirror_morphed[i] && rs->mirror_source_tid[i] >= 0)
                        display_tid = rs->mirror_source_tid[i];
                    rs->trick_transmute_ids[i] =
                        (ttd && ttd->effect == TEFFECT_FOG_HIDDEN) ? -1 : display_tid;
                }
                int ttid = pls->current_tti.transmutation_ids[i];
                if (ttid >= 0) {
                    if (pls->current_tti.fogged[i]) {
                        int fog_tp = pls->current_tti.fog_transmuter[i];
                        rs->trick_fog_mode[i] = (fog_tp == 0) ? 1 : 2;
                    } else if (pls->current_tti.resolved_effects[i] == TEFFECT_FOG_HIDDEN) {
                        int ttp = pls->current_tti.transmuter_player[i];
                        rs->trick_fog_mode[i] = (ttp == 0) ? 1 : 2;
                    } else {
                        rs->trick_fog_mode[i] = 0;
                    }
                } else {
                    rs->trick_fog_mode[i] = 0;
                }
            }

        }

    } else {
        rs->info_contract_count = 0;
        rs->transmute_btn_count = 0;
        rs->pending_transmutation_id = -1;
        for (int i = 0; i < MAX_HAND_SIZE; i++) {
            rs->hand_transmute_ids[i] = -1;
            rs->hand_fog_mode[i] = 0;
        }
        for (int i = 0; i < CARDS_PER_TRICK; i++) {
            rs->trick_transmute_ids[i] = -1;
            rs->trick_fog_mode[i] = 0;
        }
    }
}

void info_sync_playability(GameState *gs, RenderState *rs, Phase2State *p2)
{
    if (gs->phase == PHASE_PLAYING) {
        const Hand *hhand = &gs->players[0].hand;
        bool ft = (gs->tricks_played == 0);
        bool is_human_turn = (game_state_current_player(gs) == 0);
        for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
            if (p2->enabled) {
                bool leading = (gs->current_trick.num_played == 0);
                bool cursed = p2->curse_force_hearts[0];
                bool anchored = p2->anchor_force_suit[0] >= 0;
                /* Curse bypasses hearts-broken restriction */
                bool hb = gs->hearts_broken || (leading && cursed);
                rs->card_playable[i] = transmute_is_valid_play(
                    &gs->current_trick, hhand,
                    &p2->players[0].hand_transmutes, i,
                    hhand->cards[i], hb, ft);
                /* Curse: only hearts allowed when leading */
                if (leading && cursed && rs->card_playable[i]) {
                    rs->card_playable[i] =
                        transmute_curse_is_valid_lead(hhand, hhand->cards[i]);
                }
                /* Anchor: only forced suit when leading (if not cursed) */
                if (leading && !cursed && anchored && rs->card_playable[i]) {
                    rs->card_playable[i] =
                        transmute_anchor_is_valid_lead(
                            hhand, hhand->cards[i],
                            p2->anchor_force_suit[0]);
                }
            } else {
                rs->card_playable[i] = game_state_is_valid_play(
                    gs, 0, hhand->cards[i]);
            }
            int idx = rs->hand_visuals[0][i];
            rs->cards[idx].dimmed = is_human_turn && !rs->card_playable[i];
        }
    } else {
        /* Clear dimmed state outside playing phase */
        for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
            int idx = rs->hand_visuals[0][i];
            rs->cards[idx].dimmed = false;
        }
    }
}
