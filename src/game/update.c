/* ============================================================
 * @deps-implements: update.h
 * @deps-requires: update.h, core/input.h, core/game_state.h (GamePhase, PHASE_SETTINGS),
 *                 core/settings.h, render/render.h, render/anim.h,
 *                 game/pass_phase.h, game/play_phase.h, game/settings_ui.h,
 *                 game/turn_flow.h, game/online_ui.h, phase2/contract_logic.h
 * @deps-last-changed: 2026-04-01
 * ============================================================ */

#include "update.h"

#include <stdio.h>
#include <string.h>

#include "core/input.h"
#include "game/friend_panel.h"
#include "net/client_net.h"
#include "net/lobby_client.h"
#include "render/render.h"
#include "phase2/contract_logic.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

void game_update(GameState *gs, RenderState *rs, Phase2State *p2,
                 PassPhaseState *pps, PlayPhaseState *pls,
                 LoginUIState *lui, OnlineUIState *oui,
                 SettingsUIState *sui, GameSettings *settings,
                 TurnFlow *flow, float dt, bool *quit_requested)
{
    /* Scoring auto-advance: push synthetic CONFIRM after 10s + timer bonus.
     * Must be outside the command loop so it ticks every frame. */
    if (gs->phase == PHASE_SCORING) {
        float bonus = flow->turn_time_limit - FLOW_TURN_TIME_LIMIT;
        rs->score_auto_limit = 10.0f + bonus;
        bool awaiting_input =
            (rs->score_subphase == SCORE_SUB_DISPLAY) ||
            (rs->score_subphase == SCORE_SUB_DONE) ||
            (rs->score_subphase == SCORE_SUB_CONTRACTS &&
             rs->contract_reveal_count >= rs->contract_result_count);
        if (awaiting_input && !rs->scoring_ready_sent) {
            rs->score_auto_timer += dt;
            if (rs->score_auto_timer >= rs->score_auto_limit) {
                InputCmd auto_cmd = {0};
                auto_cmd.type = INPUT_CMD_CONFIRM;
                input_cmd_push(auto_cmd);
                rs->score_auto_timer = 0.0f;
            }
        }
    }

    /* Update friend panel for all pre-game phases */
    {
        bool pregame = (gs->phase == PHASE_MENU ||
                        gs->phase == PHASE_ONLINE_MENU ||
                        gs->phase == PHASE_STATS ||
                        gs->phase == PHASE_SETTINGS);
        if (pregame) {
            friend_panel_update(&oui->friend_panel, dt);
        }
    }

    InputCmd cmd;
    for (int n = 0; n < INPUT_CMD_QUEUE_CAPACITY &&
                    (cmd = input_cmd_pop()).type != INPUT_CMD_NONE; n++) {

        /* ESC during in-game phase → open pause menu */
        if (cmd.type == INPUT_CMD_CANCEL && is_ingame_phase(gs->phase) &&
            rs->pause_state == PAUSE_INACTIVE) {
            rs->pause_state = PAUSE_MENU;
            if (rs->drag.active) render_cancel_drag(rs);
            continue;
        }

        /* Handle menu commands while paused during in-game phases */
        if (rs->pause_state != PAUSE_INACTIVE && is_ingame_phase(gs->phase)) {
            bool menu_cmd = false;
            if (cmd.type == INPUT_CMD_CANCEL) {
                /* ESC: unpause, or back from confirmation to pause menu */
                if (rs->pause_state == PAUSE_MENU)
                    rs->pause_state = PAUSE_INACTIVE;
                else
                    rs->pause_state = PAUSE_MENU;
                menu_cmd = true;
            } else if (cmd.type == INPUT_CMD_OPEN_SETTINGS) {
                rs->settings_return_phase = gs->phase;
                rs->settings_return_paused = true;
                rs->pause_state = PAUSE_INACTIVE;
                gs->phase = PHASE_SETTINGS;
                rs->settings_tab = SETTINGS_TAB_DISPLAY;
                sync_settings_values(sui, settings, rs);
                input_cmd_queue_clear();
                menu_cmd = true;
            } else if (cmd.type == INPUT_CMD_RETURN_TO_MENU) {
                pps->subphase = PASS_SUB_DEALER;
                pps->draft_pick_pending = false;
                pps->pass_anim = false;
                pps->async_toss = false;
                pps->toss_count = 0;
                pps->pass_auto_sent = false;
                memset(pps->toss_started, 0, sizeof(pps->toss_started));
                pls->pending_transmutation = -1;
                for (int ti = 0; ti < CARDS_PER_TRICK; ti++) {
                    pls->current_tti.transmutation_ids[ti] = -1;
                    pls->current_tti.transmuter_player[ti] = -1;
                    pls->current_tti.resolved_effects[ti] = TEFFECT_NONE;
                }
                game_state_reset_to_menu(gs);
                render_reset_to_menu(rs);
                input_cmd_queue_clear();
                menu_cmd = true;
            } else if (cmd.type == INPUT_CMD_QUIT) {
                *quit_requested = true;
                menu_cmd = true;
            }
            if (menu_cmd)
                continue;
        }

        switch (gs->phase) {
        case PHASE_LOGIN:
            /* Login submit/retry handled by main.c lobby state machine. */
            if (cmd.type == INPUT_CMD_QUIT || cmd.type == INPUT_CMD_CANCEL) {
                *quit_requested = true;
            }
            (void)lui;
            break;

        case PHASE_ONLINE_MENU:
            /* Online menu commands handled by main.c state machine. */
            if (cmd.type == INPUT_CMD_QUIT) {
                *quit_requested = true;
            }
            {
                bool in_room = (oui->subphase == ONLINE_SUB_CREATE_WAITING ||
                                oui->subphase == ONLINE_SUB_CONNECTED_WAITING);
                friend_panel_set_can_invite(&oui->friend_panel, in_room);
                if (in_room) {
                    memcpy(oui->friend_panel.current_room_code,
                           oui->created_room_code[0] ? oui->created_room_code : oui->assigned_room_code, 8);
                }
            }
            break;

        case PHASE_MENU:
            if (cmd.type == INPUT_CMD_OPEN_PLAY) {
                gs->phase = PHASE_ONLINE_MENU;
                rs->sync_needed = true;
            } else if (cmd.type == INPUT_CMD_OPEN_STATS) {
                gs->phase = PHASE_STATS;
                rs->stats_tab = STATS_TAB_GAME_STATS;
                rs->leaderboard_loaded = false;
                rs->leaderboard_loading = false;
                rs->leaderboard_scroll_y = 0.0f;
                if (lobby_client_state() == LOBBY_AUTHENTICATED) {
                    rs->stats_loaded = false;
                    rs->stats_loading = true;
                    lobby_client_request_stats();
                } else {
                    rs->stats_loaded = false;
                    rs->stats_loading = false;
                }
                rs->sync_needed = true;
            } else if (cmd.type == INPUT_CMD_OPEN_SETTINGS) {
                rs->settings_return_phase = PHASE_MENU;
                rs->settings_return_paused = false;
                gs->phase = PHASE_SETTINGS;
                rs->settings_tab = SETTINGS_TAB_DISPLAY;
                rs->sync_needed = true;
                sync_settings_values(sui, settings, rs);
            } else if (cmd.type == INPUT_CMD_QUIT ||
                       cmd.type == INPUT_CMD_CANCEL) {
                *quit_requested = true;
            }
            break;

        case PHASE_STATS:
            if (cmd.type == INPUT_CMD_CANCEL) {
                gs->phase = PHASE_MENU;
                rs->sync_needed = true;
            } else if (cmd.type == INPUT_CMD_QUIT) {
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
                gs->phase = rs->settings_return_phase;
                if (rs->settings_return_paused)
                    rs->pause_state = PAUSE_MENU;
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
            if (cmd.type == INPUT_CMD_CONFIRM &&
                pps->subphase == PASS_SUB_CARD_PASS &&
                rs->selected_count == gs->pass_card_count) {
                Card pass_cards[MAX_PASS_CARD_COUNT];
                bool valid = true;
                for (int i = 0; i < gs->pass_card_count; i++) {
                    int idx = rs->selected_indices[i];
                    if (idx < 0 || idx >= rs->card_count) {
                        valid = false;
                        break;
                    }
                    pass_cards[i] = rs->cards[idx].card;
                }
                if (!valid) break;
                /* Set transmutation hints before select_pass for disambiguation */
                for (int i = 0; i < gs->pass_card_count; i++) {
                    int vi = rs->selected_indices[i];
                    gs->pass_selection_hints[0][i] = -1;
                    if (p2->enabled) {
                        int hand_idx = -1;
                        for (int ci = 0; ci < rs->hand_visual_counts[0]; ci++) {
                            if (rs->hand_visuals[0][ci] == vi) {
                                hand_idx = ci;
                                break;
                            }
                        }
                        if (hand_idx >= 0) {
                            const HandTransmuteState *hts = &p2->players[0].hand_transmutes;
                            if (transmute_is_transmuted(hts, hand_idx))
                                gs->pass_selection_hints[0][i] = hts->slots[hand_idx].transmutation_id;
                        }
                    }
                }
                game_state_select_pass(gs, 0, pass_cards, gs->pass_card_count);
                render_clear_selection(rs);
                pls->pending_transmutation = -1;
                if (gs->pass_card_count == 0) {
                    rs->pass_ready_waiting = true;
                }
            }
            break;

        case PHASE_PLAYING:
            if (cmd.type == INPUT_CMD_PLAY_CARD &&
                cmd.source_player == 0) {
                if (!play_card_with_transmute(gs, rs, p2, pls, 0,
                                              cmd.card.card,
                                              cmd.card.card_index)) {
                    rs->sync_needed = true;
                }
            }
            break;

        case PHASE_SCORING:
            if (cmd.type == INPUT_CMD_CONFIRM) {
                if (rs->score_subphase == SCORE_SUB_DISPLAY) {
                    /* Start count-up animation */
                    rs->score_subphase = SCORE_SUB_COUNT_UP;
                    rs->btn_continue.visible = false;
                    rs->score_countup_timer = 0.0f;
                    rs->score_auto_timer = 0.0f;
                } else if (rs->score_subphase == SCORE_SUB_DONE) {
                    /* Advance to next round (show contracts if Phase 2) */
                    if (p2->enabled && !game_state_is_game_over(gs)) {
                        /* Server already evaluated contracts — use server-provided data */

                        /* Populate result fields from p2 */
                        int result_idx = 0;
                        for (int i = 0; i < NUM_PLAYERS; i++) {
                            PlayerPhase2 *pp = &p2->players[i];
                            for (int c = 0; c < pp->num_active_contracts && result_idx < MAX_CONTRACT_RESULTS; c++) {
                                const ContractInstance *ci = &pp->contracts[c];
                                if (ci->contract_id >= 0 && ci->completed) {
                                    const ContractDef *cd = phase2_get_contract(ci->contract_id);
                                    const TransmutationDef *td = (ci->paired_transmutation_id >= 0)
                                        ? phase2_get_transmutation(ci->paired_transmutation_id) : NULL;
                                    const char *tmute_name = td ? td->name : (cd ? cd->name : "Unknown");
                                    const char *desc = cd ? cd->description : "";

                                    /* Player name in text field */
                                    snprintf(rs->contract_result_text[result_idx],
                                             sizeof(rs->contract_result_text[result_idx]),
                                             "%s", p2_player_name(i, rs));
                                    snprintf(rs->contract_result_name[result_idx],
                                             sizeof(rs->contract_result_name[result_idx]),
                                             "%s", tmute_name);
                                    snprintf(rs->contract_result_desc[result_idx],
                                             sizeof(rs->contract_result_desc[result_idx]),
                                             "%s", desc);
                                    snprintf(rs->contract_result_tdesc[result_idx],
                                             sizeof(rs->contract_result_tdesc[result_idx]),
                                             "%s", td ? td->description : "");
                                    rs->contract_result_success[result_idx] = true;
                                    result_idx++;

                                    /* Contract rewards shown in reward panel only */
                                }
                            }
                        }
                        rs->contract_result_count = result_idx;
                        rs->show_contract_results = true;

                        /* Switch to contracts panel with staggered reveal */
                        rs->score_subphase = SCORE_SUB_CONTRACTS;
                        rs->contract_reveal_count = 0;
                        rs->contract_scroll_y = 0.0f;
                        rs->contract_reveal_timer =
                            ANIM_CONTRACT_REVEAL_STAGGER * anim_get_speed();
                        rs->btn_continue.visible = false;
                        rs->btn_continue.label = "Next Round";
                        rs->score_auto_timer = 0.0f;
                    } else {
                        rs->scoring_screen_done = true;
                        rs->sync_needed = true;
                    }
                } else if (rs->score_subphase == SCORE_SUB_CONTRACTS) {
                    /* Block confirm until all rows revealed */
                    if (rs->contract_reveal_count < rs->contract_result_count) break;
                    /* Advance to next round */
                    rs->show_contract_results = false;
                    rs->scoring_screen_done = true;
                    rs->sync_needed = true;
                }
                input_cmd_queue_clear();
                goto done_processing;
            }
            break;

        case PHASE_GAME_OVER:
            if (cmd.type == INPUT_CMD_CONFIRM) {
                /* Disconnect from game server if this was an online game */
                if (client_net_state() == CLIENT_NET_CONNECTED) {
                    client_net_disconnect();
                }
                settings_clear_reconnect();
                settings->reconnect.valid = false;
                game_state_reset_to_menu(gs);
                render_chat_log_clear(rs);
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
