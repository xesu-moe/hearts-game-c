/* ============================================================
 * @deps-implements: info_sync.h
 * @deps-requires: info_sync.h, core/game_state.h, render/render.h,
 *                 phase2/phase2_state.h (PlayerPhase2.contracts[],
 *                   PlayerPhase2.num_active_contracts, curse_force_hearts[],
 *                   anchor_force_suit[]),
 *                 phase2/phase2_defs.h (phase2_get_contract, phase2_get_transmutation),
 *                 phase2/vendetta_logic.h, phase2/transmutation_logic.h,
 *                 phase2/transmutation.h, stdio.h
 * @deps-last-changed: 2026-03-21 — Updated for multi-contract info panel sync (info_contract_count)
 * ============================================================ */

#include "info_sync.h"

#include <stdio.h>

#include "render/render.h"
#include "phase2/phase2_defs.h"
#include "phase2/vendetta_logic.h"
#include "phase2/transmutation_logic.h"

void info_sync_update(GameState *gs, RenderState *rs, Phase2State *p2,
                      PlayPhaseState *pls)
{
    if (p2->enabled) {
        /* Contracts (up to 3) */
        rs->info_contract_count = 0;
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
                    rs->info_contract_count++;
                }
            }
        }

        /* Vendetta action */
        if (p2->round.chosen_vendetta >= 0) {
            const VendettaDef *vd =
                phase2_get_vendetta(p2->round.chosen_vendetta);
            if (vd) {
                rs->info_vendetta_active = true;
                snprintf(rs->info_vendetta_name,
                         sizeof(rs->info_vendetta_name), "%s", vd->name);
                snprintf(rs->info_vendetta_desc,
                         sizeof(rs->info_vendetta_desc), "%s",
                         vd->description);
            } else {
                rs->info_vendetta_active = false;
            }
        } else {
            rs->info_vendetta_active = false;
        }

        /* Shield state */
        for (int i = 0; i < NUM_PLAYERS; i++) {
            rs->shield_remaining[i] = p2->shield_tricks_remaining[i];
        }

        /* Bonuses (persistent effects) */
        rs->info_bonus_count = 0;
        for (int i = 0;
             i < p2->players[0].num_persistent && i < INFO_BONUS_MAX;
             i++) {
            if (!p2->players[0].persistent_effects[i].active) continue;
            render_effect_label(
                &p2->players[0].persistent_effects[i],
                rs->info_bonus_text[rs->info_bonus_count], 48);
            rs->info_bonus_count++;
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
                    snprintf(rs->transmute_btn_labels[tcount], 32, "%s", td->name);
                    rs->transmute_btns[tcount].label =
                        rs->transmute_btn_labels[tcount];
                    tcount++;
                }
            }
            rs->transmute_btn_count = tcount;
            rs->pending_transmutation_id = pls->pending_transmutation;

            /* Hand transmute IDs + fog mode for player 0 */
            HandTransmuteState *hts = &p2->players[0].hand_transmutes;
            for (int i = 0; i < MAX_HAND_SIZE; i++) {
                rs->hand_transmute_ids[i] =
                    (i < gs->players[0].hand.count &&
                     hts->slots[i].transmutation_id >= 0)
                        ? hts->slots[i].transmutation_id
                        : -1;
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

            /* Trick transmute IDs + fog mode */
            for (int i = 0; i < CARDS_PER_TRICK; i++) {
                rs->trick_transmute_ids[i] =
                    pls->current_tti.transmutation_ids[i];
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

            /* Transmutation card descriptions for info panel */
            rs->transmute_info_count = 0;
            int seen_tids[MAX_TRANSMUTE_INFO];
            int seen_count = 0;
            for (int i = 0; i < gs->players[0].hand.count &&
                 rs->transmute_info_count < MAX_TRANSMUTE_INFO &&
                 seen_count < MAX_TRANSMUTE_INFO; i++) {
                int tid = hts->slots[i].transmutation_id;
                if (tid < 0) continue;
                bool dup = false;
                for (int d = 0; d < seen_count; d++) {
                    if (seen_tids[d] == tid) { dup = true; break; }
                }
                if (dup) continue;
                const TransmutationDef *td = phase2_get_transmutation(tid);
                if (td) {
                    seen_tids[seen_count++] = tid;
                    snprintf(rs->transmute_info_text[rs->transmute_info_count],
                             64, "%d: %.50s", tid, td->description);
                    rs->transmute_info_count++;
                }
            }
        }

        /* Vendetta options for human player during PLAYING phase */
        if (p2->round.vendetta_player_id == 0 &&
            !p2->round.vendetta_used &&
            gs->phase == PHASE_PLAYING) {
            int ids[MAX_VENDETTA_OPTIONS];
            int cnt = vendetta_get_available(p2, 0,
                VENDETTA_TIMING_PLAYING, ids);
            if (cnt > 0) {
                rs->vendetta_available = true;
                rs->vendetta_count = cnt;
                for (int i = 0; i < cnt; i++) {
                    rs->vendetta_ids[i] = ids[i];
                    const VendettaDef *vd = phase2_get_vendetta(ids[i]);
                    rs->vendetta_btns[i].label =
                        vd ? vd->name : "???";
                    rs->vendetta_btns[i].subtitle =
                        vd ? vd->description : "";
                    rs->vendetta_btns[i].visible = true;
                    rs->vendetta_btns[i].disabled =
                        !rs->vendetta_interactive;
                }
                rs->vendetta_skip_btn.label = "Save for later";
                rs->vendetta_skip_btn.visible = true;
                rs->vendetta_skip_btn.disabled =
                    !rs->vendetta_interactive;
            } else {
                rs->vendetta_available = false;
                rs->vendetta_count = 0;
                rs->vendetta_skip_btn.visible = false;
            }
        } else {
            rs->vendetta_available = false;
            rs->vendetta_count = 0;
            rs->vendetta_skip_btn.visible = false;
        }
    } else {
        rs->info_contract_count = 0;
        rs->info_vendetta_active = false;
        rs->info_bonus_count = 0;
        rs->vendetta_available = false;
        rs->vendetta_count = 0;
        rs->transmute_btn_count = 0;
        rs->pending_transmutation_id = -1;
        rs->transmute_info_count = 0;
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
