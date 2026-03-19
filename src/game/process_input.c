/* ============================================================
 * @deps-implements: process_input.h
 * @deps-requires: process_input.h, core/input.h, core/game_state.h,
 *                 render/render.h (render_start_card_drag, render_commit_hand_reorder,
 *                                  render_update_snap_target, render_cancel_drag),
 *                 render/layout.h (layout_trick_position),
 *                 game/pass_phase.h, game/play_phase.h, game/turn_flow.h
 * @deps-last-changed: 2026-03-19 — Moved drag helpers to render.c; process_input now calls render API
 * ============================================================ */

#include "process_input.h"

#include <math.h>

#include "core/input.h"
#include "render/render.h"
#include "render/layout.h"

/* Toss classification thresholds */
#define TOSS_CLICK_DIST   10.0f
#define TOSS_MIN_SPEED    400.0f
#define TOSS_DROP_RADIUS  150.0f
#define TOSS_MIN_UPWARD   0.3f  /* minimum -vy/speed ratio to count as a flick (roughly 17 degrees from horizontal) */

void process_input(GameState *gs, RenderState *rs,
                   PassPhaseState *pps, PlayPhaseState *pls,
                   Phase2State *p2, FlowStep flow_step)
{
    input_poll();

    const InputState *is = input_get_state();

    /* Handle drag release — must come before new press detection */
    if (is->released[INPUT_ACTION_LEFT_CLICK] && rs->drag.active) {
        int dvi = rs->drag.card_visual_idx;
        if (dvi < 0 || dvi >= rs->card_count) {
            render_cancel_drag(rs);
        } else {
            float dx = rs->drag.current_pos.x - rs->drag.original_pos.x;
            float dy = rs->drag.current_pos.y - rs->drag.original_pos.y;
            float drag_dist = sqrtf(dx*dx + dy*dy);

            if (rs->drag.is_play_drag) {
                /* Play-drag: toss classification as before */
                float speed = sqrtf(rs->drag.velocity.x * rs->drag.velocity.x +
                                    rs->drag.velocity.y * rs->drag.velocity.y);

                Vector2 board_center = layout_trick_position(POS_BOTTOM, &rs->layout);
                float bx = rs->drag.current_pos.x - board_center.x;
                float by = rs->drag.current_pos.y - board_center.y;
                float board_dist = sqrtf(bx*bx + by*by);

                int mode;
                /* Flick requires upward component: -vy/speed >= threshold */
                float upward_ratio = (speed > 0.0f) ? (-rs->drag.velocity.y / speed) : 0.0f;

                if (drag_dist < TOSS_CLICK_DIST)       mode = TOSS_CLICK;
                else if (speed >= TOSS_MIN_SPEED && upward_ratio >= TOSS_MIN_UPWARD)
                                                        mode = TOSS_FLICK;
                else if (board_dist < TOSS_DROP_RADIUS) mode = TOSS_DROP;
                else                                    mode = TOSS_CANCEL;

                if (mode != TOSS_CANCEL) {
                    /* Commit reorder before playing */
                    render_commit_hand_reorder(gs, rs, p2);

                    rs->drag.release_pos = rs->drag.current_pos;
                    rs->drag.has_release_pos = true;
                    rs->drag.release_mode = mode;
                    rs->drag.active = false;

                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_PLAY_CARD,
                        .source_player = 0,
                        .card = {
                            .card_index = -1,
                            .card = rs->cards[dvi].card,
                        },
                    });
                    rs->drag.card_visual_idx = -1;
                } else {
                    /* Cancel toss: commit reorder and snap to new slot */
                    render_commit_hand_reorder(gs, rs, p2);
                    render_update_snap_target(rs);
                    rs->drag.snap_back = true;
                    rs->drag.active = false;
                }
            } else {
                /* Non-play drag (rearrange only) */
                if (drag_dist < TOSS_CLICK_DIST && gs->phase == PHASE_PASSING) {
                    /* Short click in passing phase: commit any reorder, then toggle */
                    render_commit_hand_reorder(gs, rs, p2);
                    render_update_snap_target(rs);
                    rs->drag.active = false;
                    rs->drag.snap_back = true;
                    render_toggle_card_selection(rs, dvi);
                } else {
                    /* Commit reorder and snap back */
                    render_commit_hand_reorder(gs, rs, p2);
                    render_update_snap_target(rs);
                    rs->drag.snap_back = true;
                    rs->drag.active = false;
                }
            }
        }
    }

    /* Generate game-layer commands from mouse clicks */
    if (is->pressed[INPUT_ACTION_LEFT_CLICK]) {
        Vector2 mouse = is->mouse_pos;

        switch (gs->phase) {
        case PHASE_MENU:
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                if (render_hit_test_button(&rs->menu_items[i], mouse)) {
                    switch ((MenuItem)i) {
                    case MENU_PLAY_OFFLINE:
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_START_GAME,
                            .source_player = 0,
                        });
                        break;
                    case MENU_SETTINGS:
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_OPEN_SETTINGS,
                            .source_player = 0,
                        });
                        break;
                    case MENU_EXIT:
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_QUIT,
                            .source_player = 0,
                        });
                        break;
                    default:
                        break;
                    }
                    break;
                }
            }
            break;

        case PHASE_PASSING: {
            if (pps->subphase == PASS_SUB_VENDETTA && pps->vendetta_ui_active) {
                int hit = render_hit_test_contract(rs, mouse);
                if (hit >= 0) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SELECT_VENDETTA,
                        .source_player = 0,
                        .vendetta = { .vendetta_id = rs->contract_option_ids[hit] },
                    });
                }
            } else if (pps->subphase == PASS_SUB_CONTRACT) {
                int contract_hit = render_hit_test_contract(rs, mouse);
                if (contract_hit >= 0) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SELECT_CONTRACT,
                        .source_player = 0,
                        .contract = { .contract_id = rs->contract_option_ids[contract_hit] },
                    });
                }
            } else if (pps->subphase == PASS_SUB_CARD_PASS) {
                /* Confirm button */
                if (render_hit_test_button(&rs->btn_confirm_pass, mouse)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_CONFIRM,
                        .source_player = 0,
                    });
                    break;
                }
                /* Transmutation inventory buttons */
                if (p2->enabled) {
                    int tmut_hit = render_hit_test_transmute(rs, mouse);
                    if (tmut_hit >= 0) {
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_SELECT_TRANSMUTATION,
                            .source_player = 0,
                            .transmute_select = { .inv_slot = tmut_hit },
                        });
                        break;
                    }
                }
                /* Card click: apply transmutation or start drag for rearrange/selection */
                int hit = render_hit_test_card(rs, mouse);
                if (hit >= 0) {
                    if (pls->pending_transmutation >= 0) {
                        int hand_idx = -1;
                        for (int ci = 0;
                             ci < rs->hand_visual_counts[0]; ci++) {
                            if (rs->hand_visuals[0][ci] == hit) {
                                hand_idx = ci;
                                break;
                            }
                        }
                        if (hand_idx >= 0) {
                            input_cmd_push((InputCmd){
                                .type = INPUT_CMD_APPLY_TRANSMUTATION,
                                .source_player = 0,
                                .transmute_apply = { .hand_index = hand_idx },
                            });
                        }
                    } else {
                        /* Start drag (short click will toggle selection on release) */
                        int hand_slot = -1;
                        for (int ci = 0; ci < rs->hand_visual_counts[0]; ci++) {
                            if (rs->hand_visuals[0][ci] == hit) {
                                hand_slot = ci;
                                break;
                            }
                        }
                        if (hand_slot >= 0) {
                            render_start_card_drag(rs, hit, hand_slot, mouse, false);
                        }
                    }
                }
            }
            break;
        }

        case PHASE_PLAYING: {
            /* Vendetta panel buttons */
            if (flow_step == FLOW_WAITING_FOR_HUMAN &&
                rs->vendetta_available && rs->vendetta_interactive) {
                bool vendetta_hit = false;
                for (int i = 0; i < rs->vendetta_count; i++) {
                    if (render_hit_test_button(&rs->vendetta_btns[i],
                                               mouse)) {
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_SELECT_VENDETTA,
                            .source_player = 0,
                            .vendetta = {
                                .vendetta_id = rs->vendetta_ids[i]},
                        });
                        vendetta_hit = true;
                        break;
                    }
                }
                if (!vendetta_hit &&
                    render_hit_test_button(&rs->vendetta_skip_btn,
                                           mouse)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SKIP_VENDETTA,
                        .source_player = 0,
                    });
                    vendetta_hit = true;
                }
                if (vendetta_hit) break;
            }
            /* Transmutation inventory buttons */
            if (p2->enabled && flow_step == FLOW_WAITING_FOR_HUMAN) {
                int tmut_hit = render_hit_test_transmute(rs, mouse);
                if (tmut_hit >= 0) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SELECT_TRANSMUTATION,
                        .source_player = 0,
                        .transmute_select = { .inv_slot = tmut_hit },
                    });
                    break;
                }
            }
            /* Card interaction: transmutation or drag */
            {
                int hit = render_hit_test_card(rs, mouse);
                if (hit >= 0) {
                    int hand_slot = -1;
                    for (int ci = 0; ci < rs->hand_visual_counts[0]; ci++) {
                        if (rs->hand_visuals[0][ci] == hit) {
                            hand_slot = ci;
                            break;
                        }
                    }
                    if (hand_slot >= 0) {
                        int current = game_state_current_player(gs);
                        bool is_human_turn = (current == 0 && flow_step == FLOW_WAITING_FOR_HUMAN);

                        if (is_human_turn && pls->pending_transmutation >= 0) {
                            input_cmd_push((InputCmd){
                                .type = INPUT_CMD_APPLY_TRANSMUTATION,
                                .source_player = 0,
                                .transmute_apply = { .hand_index = hand_slot },
                            });
                        } else {
                            /* Drag any card; only play-drag if it's our turn and card is playable */
                            bool play_drag = is_human_turn && rs->card_playable[hand_slot];
                            render_start_card_drag(rs, hit, hand_slot, mouse, play_drag);
                        }
                    }
                }
            }
            break;
        }

        case PHASE_SETTINGS:
            if (render_hit_test_button(&rs->btn_settings_back, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_CANCEL,
                    .source_player = 0,
                });
                break;
            }
            if (render_hit_test_button(&rs->btn_settings_apply, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_APPLY_DISPLAY,
                    .source_player = 0,
                });
                break;
            }
            for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
                if (render_hit_test_button(&rs->settings_rows_prev[i], mouse)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SETTING_PREV,
                        .source_player = 0,
                        .setting = {.setting_id = i},
                    });
                    break;
                }
                if (render_hit_test_button(&rs->settings_rows_next[i], mouse)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SETTING_NEXT,
                        .source_player = 0,
                        .setting = {.setting_id = i},
                    });
                    break;
                }
            }
            break;

        case PHASE_SCORING:
        case PHASE_GAME_OVER:
            if (render_hit_test_button(&rs->btn_continue, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_CONFIRM,
                    .source_player = 0,
                });
            }
            break;

        default:
            break;
        }
    }

}
