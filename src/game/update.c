/* ============================================================
 * @deps-implements: update.h
 * @deps-requires: update.h, input.h (INPUT_CMD_SELECT_CONTRACT, INPUT_CMD_DUEL_*,
 *                 INPUT_CMD_OPEN_STATS, INPUT_CMD_ONLINE_*), game_state.h (PHASE_STATS),
 *                 settings.h, render.h, anim.h, pass_phase.h, play_phase.h,
 *                 settings_ui.h, turn_flow.h, online_ui.h, phase2/contract_logic.h,
 *                 phase2/phase2_state.h, phase2/transmutation_logic.h, phase2/phase2_defs.h
 * @deps-last-changed: 2026-03-26 — Step 21: Added INPUT_CMD_OPEN_STATS handler for PHASE_STATS transition
 * ============================================================ */

#include "update.h"

#include <stdio.h>

#include "core/input.h"
#include "net/client_net.h"
#include "render/render.h"
#include "ai.h"
#include "phase2/contract_logic.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

void game_update(GameState *gs, RenderState *rs, Phase2State *p2,
                 PassPhaseState *pps, PlayPhaseState *pls,
                 LoginUIState *lui, OnlineUIState *oui,
                 SettingsUIState *sui, GameSettings *settings,
                 TurnFlow *flow, float dt, bool *quit_requested)
{
    (void)dt;

#ifdef DEBUG
    /* F5: skip to last trick of the round */
    if (gs->phase == PHASE_PLAYING && IsKeyPressed(KEY_F5)) {
        while (gs->phase == PHASE_PLAYING && gs->tricks_played < 12) {
            /* Fill current trick with AI plays */
            while (gs->current_trick.num_played < NUM_PLAYERS &&
                   gs->phase == PHASE_PLAYING) {
                int p = game_state_current_player(gs);
                if (p < 0) break;
                ai_play_card(gs, rs, p2, pls, p);
            }
            /* Complete the trick */
            if (gs->current_trick.num_played >= NUM_PLAYERS)
                game_state_complete_trick(gs);
        }
        /* Reset flow for the last trick */
        flow_init(flow);
        rs->sync_needed = true;
        input_cmd_queue_clear();
    }
#endif

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
                rs->sync_needed = true;
                sync_settings_values(sui, settings, rs);
                input_cmd_queue_clear();
                menu_cmd = true;
            } else if (cmd.type == INPUT_CMD_RETURN_TO_MENU) {
                pps->subphase = PASS_SUB_DEALER;
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
            /* Offline: intercept all commands while paused.
             * Online: only consume menu commands, let game commands through. */
            if (menu_cmd || !rs->online)
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
            (void)oui;
            break;

        case PHASE_MENU:
            if (cmd.type == INPUT_CMD_START_GAME ||
                cmd.type == INPUT_CMD_CONFIRM) {
                game_state_start_game(gs);
                rs->sync_needed = true;
            } else if (cmd.type == INPUT_CMD_OPEN_ONLINE) {
                gs->phase = PHASE_ONLINE_MENU;
                rs->sync_needed = true;
            } else if (cmd.type == INPUT_CMD_OPEN_STATS) {
                gs->phase = PHASE_STATS;
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
            if (cmd.type == INPUT_CMD_SELECT_CONTRACT && p2->enabled) {
                int pair_idx = cmd.contract.pair_index;
                if (pps->subphase == PASS_SUB_CONTRACT &&
                    p2->round.draft.active) {
                    DraftState *draft = &p2->round.draft;
                    DraftPlayerState *ps = &draft->players[0];

                    if (pair_idx >= 0 && pair_idx < ps->available_count) {
                        draft_pick(draft, 0, pair_idx);
                        rs->selected_contract_idx = pair_idx;

                        if (draft_all_picked(draft))
                            draft_finish_round(pps, gs, rs, p2);
                        else
                            setup_draft_ui(rs, p2);
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
                                hand_idx, tid, 0);
                rs->sync_needed = true;
                pls->pending_transmutation = -1;
            }
            /* Dealer input */
            if (cmd.type == INPUT_CMD_DEALER_DIR &&
                pps->subphase == PASS_SUB_DEALER && pps->dealer_ui_active) {
                pps->dealer_dir = cmd.dealer_dir.direction;
            }
            if (cmd.type == INPUT_CMD_DEALER_AMT &&
                pps->subphase == PASS_SUB_DEALER && pps->dealer_ui_active) {
                pps->dealer_amt = cmd.dealer_amt.amount;
            }
            if (cmd.type == INPUT_CMD_DEALER_CONFIRM &&
                pps->subphase == PASS_SUB_DEALER && pps->dealer_ui_active) {
                gs->pass_direction = (PassDirection)pps->dealer_dir;
                gs->pass_card_count = pps->dealer_amt;
                dealer_announce(pps, rs);
            }
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
                game_state_select_pass(gs, 0, pass_cards, gs->pass_card_count);
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
                                hand_idx, tid, 0);
                rs->sync_needed = true;
                pls->pending_transmutation = -1;
            } else if (cmd.type == INPUT_CMD_ROGUE_REVEAL && flow &&
                       flow->step == FLOW_ROGUE_CHOOSING) {
                int tp = cmd.rogue_reveal.target_player;
                int hi = cmd.rogue_reveal.hand_index;
                if (tp > 0 && tp < NUM_PLAYERS && tp != flow->rogue_winner &&
                    hi >= 0 && hi < gs->players[tp].hand.count) {
                    flow->rogue_reveal_player = tp;
                    flow->rogue_reveal_card_idx = hi;
                }
            } else if (cmd.type == INPUT_CMD_DUEL_PICK && flow &&
                       flow->step == FLOW_DUEL_PICK_OPPONENT) {
                int tp = cmd.duel_pick.target_player;
                int hi = cmd.duel_pick.hand_index;
                if (tp > 0 && tp < NUM_PLAYERS && tp != flow->duel_winner &&
                    hi >= 0 && hi < gs->players[tp].hand.count) {
                    flow->duel_target_player = tp;
                    flow->duel_target_card_idx = hi;
                }
            } else if (cmd.type == INPUT_CMD_DUEL_GIVE && flow &&
                       flow->step == FLOW_DUEL_PICK_OWN) {
                int hi = cmd.duel_give.hand_index;
                if (hi >= 0 && hi < gs->players[0].hand.count) {
                    flow->duel_own_card_idx = hi;
                }
            } else if (cmd.type == INPUT_CMD_DUEL_RETURN && flow &&
                       flow->step == FLOW_DUEL_PICK_OWN) {
                flow->duel_returned = true;
            }
            break;

        case PHASE_SCORING:
            if (cmd.type == INPUT_CMD_CONFIRM) {
                if (rs->score_subphase == SCORE_SUB_DISPLAY) {
                    /* Start count-up animation */
                    rs->score_subphase = SCORE_SUB_COUNT_UP;
                    rs->btn_continue.visible = false;
                    rs->score_countup_timer = 0.0f;
                } else if (rs->score_subphase == SCORE_SUB_DONE) {
                    /* Advance to next round (show contracts if Phase 2) */
                    if (p2->enabled && !game_state_is_game_over(gs)) {
                        /* Evaluate all contracts and populate result fields
                         * (only completed contracts are shown) */
                        int result_idx = 0;
                        for (int i = 0; i < NUM_PLAYERS; i++) {
                            contract_evaluate_all(p2, i);
                            contract_apply_rewards_all(p2, i);

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
                                             "%s", p2_player_name(i));
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

                                    /* Chat log */
                                    char chat_msg[CHAT_MSG_LEN];
                                    snprintf(chat_msg, sizeof(chat_msg),
                                             "%s obtained %s",
                                             p2_player_name(i), tmute_name);
                                    render_chat_log_push(rs, chat_msg);
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
                    } else {
                        game_state_advance_scoring(gs);
                        rs->sync_needed = true;
                    }
                } else if (rs->score_subphase == SCORE_SUB_CONTRACTS) {
                    /* Block confirm until all rows revealed */
                    if (rs->contract_reveal_count < rs->contract_result_count) break;
                    /* Advance to next round */
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
                /* Disconnect from game server if this was an online game */
                if (client_net_state() == CLIENT_NET_CONNECTED)
                    client_net_disconnect();
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
