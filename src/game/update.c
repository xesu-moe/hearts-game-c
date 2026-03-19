/* ============================================================
 * @deps-implements: update.h
 * @deps-requires: update.h, core/input.h, core/game_state.h, core/settings.h,
 *                 render/render.h, game/pass_phase.h, game/play_phase.h,
 *                 game/settings_ui.h, phase2/contract_logic.h,
 *                 phase2/vendetta_logic.h, phase2/transmutation_logic.h,
 *                 phase2/phase2_defs.h
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "update.h"

#include <stdio.h>

#include "core/input.h"
#include "render/render.h"
#include "phase2/contract_logic.h"
#include "phase2/vendetta_logic.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

void game_update(GameState *gs, RenderState *rs, Phase2State *p2,
                 PassPhaseState *pps, PlayPhaseState *pls,
                 SettingsUIState *sui, GameSettings *settings,
                 float dt, bool *quit_requested)
{
    (void)dt;

    InputCmd cmd;
    for (int n = 0; n < INPUT_CMD_QUEUE_CAPACITY &&
                    (cmd = input_cmd_pop()).type != INPUT_CMD_NONE; n++) {
        switch (gs->phase) {
        case PHASE_MENU:
            if (cmd.type == INPUT_CMD_START_GAME ||
                cmd.type == INPUT_CMD_CONFIRM) {
                game_state_start_game(gs);
                rs->sync_needed = true;
            } else if (cmd.type == INPUT_CMD_OPEN_SETTINGS) {
                gs->phase = PHASE_SETTINGS;
                rs->sync_needed = true;
                sync_settings_values(sui, settings, rs);
            } else if (cmd.type == INPUT_CMD_QUIT ||
                       cmd.type == INPUT_CMD_CANCEL) {
                *quit_requested = true;
            }
            break;

        case PHASE_SETTINGS:
            if (cmd.type == INPUT_CMD_CANCEL) {
                sui->is_pending = false;
                if (settings->dirty) {
                    settings_save(settings);
                    settings->dirty = false;
                }
                gs->phase = PHASE_MENU;
                rs->sync_needed = true;
            } else if (cmd.type == INPUT_CMD_APPLY_DISPLAY) {
                apply_display_settings(sui, settings, rs);
            } else if (cmd.type == INPUT_CMD_SETTING_PREV) {
                setting_adjust(sui, settings, cmd.setting.setting_id, -1, rs);
            } else if (cmd.type == INPUT_CMD_SETTING_NEXT) {
                setting_adjust(sui, settings, cmd.setting.setting_id, 1, rs);
            }
            break;

        case PHASE_DEALING:
            break;

        case PHASE_PASSING:
            if (cmd.type == INPUT_CMD_SELECT_VENDETTA && p2->enabled &&
                pps->subphase == PASS_SUB_VENDETTA && pps->vendetta_ui_active) {
                vendetta_select(p2, cmd.vendetta.vendetta_id);
                vendetta_apply(p2);
                pps->vendetta_ui_active = false;
                advance_pass_subphase(pps, gs, rs, p2, PASS_SUB_CONTRACT);
            }
            if (cmd.type == INPUT_CMD_SELECT_CONTRACT && p2->enabled) {
                int cid = cmd.contract.contract_id;
                if (pps->subphase == PASS_SUB_CONTRACT) {
                    for (int i = 0; i < rs->contract_option_count; i++) {
                        if (rs->contract_option_ids[i] == cid) {
                            rs->selected_contract_idx = i;
                            contract_select(p2, 0, cid);

                            for (int p = 1; p < NUM_PLAYERS; p++) {
                                if (p2->players[p].contract.contract_id < 0) {
                                    contract_ai_select(p2, p);
                                }
                            }
                            p2->round.contracts_chosen =
                                contract_all_chosen(p2);
                            rs->contract_ui_active = false;

                            if (gs->pass_direction == PASS_NONE) {
                                gs->phase = PHASE_PLAYING;
                                rs->sync_needed = true;
                            } else {
                                advance_pass_subphase(pps, gs, rs, p2,
                                                       PASS_SUB_CARD_PASS);
                            }
                            break;
                        }
                    }
                }
            }
            if (cmd.type == INPUT_CMD_SELECT_TRANSMUTATION &&
                p2->enabled &&
                pps->subphase == PASS_SUB_CARD_PASS) {
                int slot = cmd.transmute_select.inv_slot;
                if (slot >= 0 &&
                    slot < p2->players[0].transmute_inv.count) {
                    pls->pending_transmutation =
                        p2->players[0].transmute_inv.items[slot];
                }
            }
            if (cmd.type == INPUT_CMD_APPLY_TRANSMUTATION &&
                p2->enabled && pls->pending_transmutation >= 0 &&
                pps->subphase == PASS_SUB_CARD_PASS) {
                int tid = pls->pending_transmutation;
                int hand_idx = cmd.transmute_apply.hand_index;
                transmute_apply(&gs->players[0].hand,
                                &p2->players[0].hand_transmutes,
                                &p2->players[0].transmute_inv,
                                hand_idx, tid);
                rs->sync_needed = true;
                pls->pending_transmutation = -1;
            }
            if (cmd.type == INPUT_CMD_CONFIRM &&
                pps->subphase == PASS_SUB_CARD_PASS &&
                rs->selected_count == PASS_CARD_COUNT) {
                Card pass_cards[PASS_CARD_COUNT];
                bool valid = true;
                for (int i = 0; i < PASS_CARD_COUNT; i++) {
                    int idx = rs->selected_indices[i];
                    if (idx < 0 || idx >= rs->card_count) {
                        valid = false;
                        break;
                    }
                    pass_cards[i] = rs->cards[idx].card;
                }
                if (!valid) break;
                game_state_select_pass(gs, 0, pass_cards);
                render_clear_selection(rs);
                pass_start_toss_anim(pps, gs, rs, p2);
            }
            break;

        case PHASE_PLAYING:
            if (cmd.type == INPUT_CMD_PLAY_CARD &&
                cmd.source_player == 0) {
                if (!play_card_with_transmute(gs, rs, p2, pls, 0,
                                              cmd.card.card)) {
                    rs->sync_needed = true;
                }
            } else if (cmd.type == INPUT_CMD_SELECT_TRANSMUTATION &&
                       p2->enabled) {
                int slot = cmd.transmute_select.inv_slot;
                if (slot >= 0 &&
                    slot < p2->players[0].transmute_inv.count) {
                    pls->pending_transmutation =
                        p2->players[0].transmute_inv.items[slot];
                }
            } else if (cmd.type == INPUT_CMD_APPLY_TRANSMUTATION &&
                       p2->enabled && pls->pending_transmutation >= 0) {
                int tid = pls->pending_transmutation;
                int hand_idx = cmd.transmute_apply.hand_index;
                transmute_apply(&gs->players[0].hand,
                                &p2->players[0].hand_transmutes,
                                &p2->players[0].transmute_inv,
                                hand_idx, tid);
                rs->sync_needed = true;
                pls->pending_transmutation = -1;
            } else if (cmd.type == INPUT_CMD_SKIP_VENDETTA) {
                p2->round.vendetta_used = true;
                p2->round.vendetta_chosen = true;
            } else if (cmd.type == INPUT_CMD_SELECT_VENDETTA) {
                vendetta_select(p2, cmd.vendetta.vendetta_id);
                vendetta_apply(p2);
                {
                    const VendettaDef *vd =
                        phase2_get_vendetta(cmd.vendetta.vendetta_id);
                    char vmsg[CHAT_MSG_LEN];
                    snprintf(vmsg, sizeof(vmsg), "%s: %s",
                             VENDETTA_DISPLAY_NAME,
                             vd ? vd->name : "???");
                    render_chat_log_push(rs, vmsg);
                }
            }
            break;

        case PHASE_SCORING:
            if (cmd.type == INPUT_CMD_CONFIRM) {
                if (p2->enabled && !rs->show_contract_results) {
                    for (int i = 0; i < NUM_PLAYERS; i++) {
                        contract_evaluate(p2, i);
                        contract_apply_reward(p2, i);

                        const ContractInstance *ci = &p2->players[i].contract;
                        if (ci->contract_id >= 0) {
                            const ContractDef *cd = phase2_get_contract(ci->contract_id);
                            const char *name = cd ? cd->name : "Unknown";
                            const char *status = ci->completed ? "Completed!" : "Failed";
                            snprintf(rs->contract_result_text[i],
                                     sizeof(rs->contract_result_text[i]),
                                     "%s: %s - %s",
                                     p2_player_name(i), name, status);
                            rs->contract_result_success[i] = ci->completed;
                        } else {
                            rs->contract_result_text[i][0] = '\0';
                        }
                    }
                    rs->show_contract_results = true;
                    for (int j = 0; j < NUM_PLAYERS; j++) {
                        if (rs->contract_result_text[j][0] != '\0') {
                            render_chat_log_push(rs,
                                                 rs->contract_result_text[j]);
                        }
                    }
                } else {
                    rs->show_contract_results = false;
                    if (p2->enabled) {
                        for (int i = 0; i < NUM_PLAYERS; i++) {
                            p2->round.prev_round_points[i] =
                                gs->players[i].round_points;
                        }
                    }
                    game_state_advance_scoring(gs);
                    rs->sync_needed = true;
                }
                input_cmd_queue_clear();
                goto done_processing;
            }
            break;

        case PHASE_GAME_OVER:
            if (cmd.type == INPUT_CMD_CONFIRM) {
                pps->vendetta_ui_active = false;
                game_state_reset_to_menu(gs);
                rs->sync_needed = true;
            }
            break;

        default:
            break;
        }
    }
done_processing:
    (void)0;
}
