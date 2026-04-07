/* ============================================================
 * @deps-implements: process_input.h
 * @deps-requires: process_input.h, input.h (INPUT_CMD_ONLINE_*, INPUT_CMD_DUEL_*,
 *                 INPUT_CMD_OPEN_STATS), game_state.h (PHASE_ONLINE_MENU, PHASE_STATS),
 *                 hand.h, render.h (hit_test_opponent_card, drag, pause, btn_stats_back),
 *                 layout.h, pass_phase.h, play_phase.h, turn_flow.h, online_ui.h, math.h
 * @deps-last-changed: 2026-03-26 — Step 21: Added PHASE_STATS back button and INPUT_CMD_OPEN_STATS handling
 * ============================================================ */

#include "process_input.h"

#include <math.h>

#include "core/input.h"
#include "game/online_ui.h"
#include "phase2/phase2_defs.h"
#include "phase2/transmutation_logic.h"
#include "render/render.h"
#include "render/layout.h"

/* Toss classification thresholds */
#define TOSS_CLICK_DIST   10.0f
#define TOSS_MIN_SPEED    400.0f
#define TOSS_DROP_RADIUS  250.0f
#define TOSS_MIN_UPWARD   0.3f  /* minimum -vy/speed ratio to count as a flick (roughly 17 degrees from horizontal) */

void process_input(GameState *gs, RenderState *rs,
                   PassPhaseState *pps, PlayPhaseState *pls,
                   Phase2State *p2, FlowStep flow_step)
{
    input_poll();

    const InputState *is = input_get_state();

    /* Clear draft click-consumed flag once mouse is released */
    if (!is->held[INPUT_ACTION_LEFT_CLICK])
        pps->draft_click_consumed = false;

    /* Cancel drag release if paused */
    if (rs->pause_state != PAUSE_INACTIVE && rs->drag.active) {
        render_cancel_drag(rs);
    }

    /* Handle drag release — must come before new press detection */
    if (is->released[INPUT_ACTION_LEFT_CLICK] && rs->drag.active) {
        if (rs->drag.is_transmute_drag) {
            /* Transmute card drag release */
            float dx = rs->drag.current_pos.x - rs->drag.original_pos.x;
            float dy = rs->drag.current_pos.y - rs->drag.original_pos.y;
            float drag_dist = sqrtf(dx*dx + dy*dy);

            /* Drag-to-apply: dropped onto a hand card */
            bool applied = false;
            int drop_vi = rs->transmute_drop_target;
            if (drop_vi >= 0 && drag_dist >= TOSS_CLICK_DIST &&
                gs->phase == PHASE_PASSING &&
                pps->subphase == PASS_SUB_CARD_PASS) {
                int hand_idx = -1;
                for (int ci = 0; ci < rs->hand_visual_counts[0]; ci++) {
                    if (rs->hand_visuals[0][ci] == drop_vi) {
                        hand_idx = ci;
                        break;
                    }
                }
                if (hand_idx >= 0) {
                    int slot = rs->drag.transmute_slot_origin;
                    int tid = (slot >= 0 && slot < rs->transmute_btn_count)
                        ? rs->transmute_btn_ids[slot] : -1;
                    Card target = gs->players[0].hand.cards[hand_idx];
                    bool blocked = false;
                    if (target.suit == SUIT_CLUBS && target.rank == RANK_2) {
                        render_chat_log_push_rich(rs,
                            "You cannot transmutate this card!",
                            (Color){230, 41, 55, 255}, NULL, -1);
                        blocked = true;
                    } else if (target.suit == SUIT_SPADES && target.rank == RANK_Q) {
                        const TransmutationDef *td = phase2_get_transmutation(tid);
                        if (!td || td->effect != TEFFECT_FOG_HIDDEN) {
                            render_chat_log_push_rich(rs,
                                "Queen of Spades can only be transmutated with The Fog!",
                                (Color){230, 41, 55, 255}, NULL, -1);
                            blocked = true;
                        }
                    }
                    if (!blocked) {
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_SELECT_TRANSMUTATION,
                            .source_player = 0,
                            .transmute_select = { .inv_slot = tid },
                        });
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_APPLY_TRANSMUTATION,
                            .source_player = 0,
                            .transmute_apply = {
                                .hand_index = hand_idx,
                                .card = gs->players[0].hand.cards[hand_idx],
                            },
                        });
                        applied = true;
                    }
                }
            } else if (drag_dist < TOSS_CLICK_DIST &&
                gs->phase == PHASE_PASSING &&
                pps->subphase == PASS_SUB_CARD_PASS) {
                /* Short click: toggle transmutation selection.
                 * Only during card-pass subphase. */
                int slot = rs->drag.transmute_slot_current;
                int clicked_id = (slot >= 0 && slot < rs->transmute_btn_count)
                    ? rs->transmute_btn_ids[slot] : -1;
                int send_id = (clicked_id >= 0 &&
                               clicked_id == rs->pending_transmutation_id)
                    ? -1 : clicked_id;
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_SELECT_TRANSMUTATION,
                    .source_player = 0,
                    .transmute_select = { .inv_slot = send_id },
                });
            }

            if (!applied)
                render_commit_transmute_reorder(rs, p2);

            rs->transmute_drop_target = -1;
            rs->drag.active = false;
            rs->drag.is_transmute_drag = false;
            rs->sync_needed = true;
        } else {
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

                /* Validate card before sending — reject stale/garbage visuals */
                if (mode != TOSS_CANCEL) {
                    Card play_card = rs->cards[dvi].card;
                    if ((int)play_card.suit < 0 || (int)play_card.suit >= SUIT_COUNT ||
                        (int)play_card.rank < 0 || (int)play_card.rank >= RANK_COUNT ||
                        !hand_contains(&gs->players[0].hand, play_card)) {
                        mode = TOSS_CANCEL;
                        rs->sync_needed = true;
                    }
                }

                if (mode != TOSS_CANCEL) {
                    Card play_card = rs->cards[dvi].card;

                    /* Commit reorder before resolving hand index —
                     * reorder shifts both hand[] and hand_visuals[] */
                    render_commit_hand_reorder(gs, rs, p2);

                    /* Resolve visual index -> hand index so we can
                     * look up transmutation state for disambiguation. */
                    int hand_idx = -1;
                    for (int ci = 0; ci < rs->hand_visual_counts[0]; ci++) {
                        if (rs->hand_visuals[0][ci] == dvi) {
                            hand_idx = ci;
                            break;
                        }
                    }

                    /* Send transmutation_id as card_index for order-independent
                     * disambiguation (-1 = non-transmuted). */
                    int hint_tid = -1;
                    if (hand_idx >= 0) {
                        const HandTransmuteState *hts = &p2->players[0].hand_transmutes;
                        if (transmute_is_transmuted(hts, hand_idx))
                            hint_tid = hts->slots[hand_idx].transmutation_id;
                    }

                    rs->drag.release_pos = rs->drag.current_pos;
                    rs->drag.has_release_pos = true;
                    rs->drag.release_mode = mode;
                    rs->drag.active = false;

                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_PLAY_CARD,
                        .source_player = 0,
                        .card = {
                            .card_index = hint_tid,
                            .card = play_card,
                        },
                    });
                    rs->drag.card_visual_idx = -1;
                } else {
                    /* Cancel toss: commit reorder and snap to new slot */
                    render_commit_hand_reorder(gs, rs, p2);
                    render_update_snap_target(rs);
                    render_snap_all_hand_cards(rs);
                    rs->drag.snap_back = true;
                    rs->drag.active = false;
                }
            } else {
                /* Non-play drag — check if it became playable during the drag
                 * (e.g., grabbed before our turn, released during our turn). */
                if (gs->phase == PHASE_PLAYING &&
                    flow_step == FLOW_WAITING_FOR_HUMAN &&
                    game_state_current_player(gs) == 0) {
                    /* Commit reorder first so hand_idx resolves correctly */
                    render_commit_hand_reorder(gs, rs, p2);

                    int hand_idx = -1;
                    for (int ci = 0; ci < rs->hand_visual_counts[0]; ci++) {
                        if (rs->hand_visuals[0][ci] == dvi) {
                            hand_idx = ci;
                            break;
                        }
                    }

                    /* Toss classification (same as play-drag path) */
                    float speed = sqrtf(rs->drag.velocity.x * rs->drag.velocity.x +
                                        rs->drag.velocity.y * rs->drag.velocity.y);
                    Vector2 board_center = layout_trick_position(POS_BOTTOM, &rs->layout);
                    float bx = rs->drag.current_pos.x - board_center.x;
                    float by = rs->drag.current_pos.y - board_center.y;
                    float board_dist = sqrtf(bx*bx + by*by);
                    float upward_ratio = (speed > 0.0f) ? (-rs->drag.velocity.y / speed) : 0.0f;

                    int mode;
                    if (drag_dist < TOSS_CLICK_DIST)       mode = TOSS_CLICK;
                    else if (speed >= TOSS_MIN_SPEED && upward_ratio >= TOSS_MIN_UPWARD)
                                                            mode = TOSS_FLICK;
                    else if (board_dist < TOSS_DROP_RADIUS) mode = TOSS_DROP;
                    else                                    mode = TOSS_CANCEL;

                    /* Only play if card is valid and toss wasn't cancelled */
                    bool can_play = (mode != TOSS_CANCEL && hand_idx >= 0 &&
                                     rs->card_playable[hand_idx]);

                    /* Validate card identity */
                    if (can_play) {
                        Card play_card = rs->cards[dvi].card;
                        if ((int)play_card.suit < 0 || (int)play_card.suit >= SUIT_COUNT ||
                            (int)play_card.rank < 0 || (int)play_card.rank >= RANK_COUNT ||
                            !hand_contains(&gs->players[0].hand, play_card)) {
                            can_play = false;
                            rs->sync_needed = true;
                        }
                    }

                    if (can_play) {
                        Card play_card = rs->cards[dvi].card;
                        int hint_tid = -1;
                        if (hand_idx >= 0) {
                            const HandTransmuteState *hts = &p2->players[0].hand_transmutes;
                            if (transmute_is_transmuted(hts, hand_idx))
                                hint_tid = hts->slots[hand_idx].transmutation_id;
                        }
                        rs->drag.release_pos = rs->drag.current_pos;
                        rs->drag.has_release_pos = true;
                        rs->drag.release_mode = mode;
                        rs->drag.active = false;
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_PLAY_CARD,
                            .source_player = 0,
                            .card = {
                                .card_index = hint_tid,
                                .card = play_card,
                            },
                        });
                        rs->drag.card_visual_idx = -1;
                    } else {
                        /* Can't play — snap back */
                        render_update_snap_target(rs);
                        render_snap_all_hand_cards(rs);
                        rs->drag.snap_back = true;
                        rs->drag.active = false;
                    }
                } else if (drag_dist < TOSS_CLICK_DIST && gs->phase == PHASE_PASSING &&
                    pps->subphase == PASS_SUB_CARD_PASS) {
                    /* Short click in passing phase: commit any reorder, then toggle */
                    render_commit_hand_reorder(gs, rs, p2);
                    render_update_snap_target(rs);
                    render_snap_all_hand_cards(rs);
                    rs->drag.active = false;
                    rs->drag.snap_back = true;
                    render_toggle_card_selection(rs, dvi);

                    /* Resolve transmutation hint for disambiguation */
                    int hint_tid = -1;
                    {
                        int hand_idx = -1;
                        for (int ci = 0; ci < rs->hand_visual_counts[0]; ci++) {
                            if (rs->hand_visuals[0][ci] == dvi) {
                                hand_idx = ci;
                                break;
                            }
                        }
                        if (hand_idx >= 0) {
                            const HandTransmuteState *hts = &p2->players[0].hand_transmutes;
                            if (transmute_is_transmuted(hts, hand_idx))
                                hint_tid = hts->slots[hand_idx].transmutation_id;
                        }
                    }
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SELECT_CARD,
                        .source_player = 0,
                        .card = {
                            .card_index = hint_tid,
                            .card = rs->cards[dvi].card,
                        },
                    });
                } else {
                    /* Commit reorder and snap back */
                    render_commit_hand_reorder(gs, rs, p2);
                    render_update_snap_target(rs);
                    render_snap_all_hand_cards(rs);
                    rs->drag.snap_back = true;
                    rs->drag.active = false;
                }
            }
        }
        } /* end else (non-transmute drag) */
    }

    /* Update opponent hover state every frame during rogue/duel selection */
    if (flow_step == FLOW_ROGUE_CHOOSING || flow_step == FLOW_DUEL_PICK_OPPONENT) {
        rs->opponent_hover_player = -1;
        Vector2 mpos = is->mouse_pos;
        for (int p = 1; p < NUM_PLAYERS; p++) {
            if (CheckCollisionPointRec(mpos, rs->opponent_indicator_rects[p])) {
                rs->opponent_hover_player = p;
                break;
            }
        }
    }

    /* Update suit hover state every frame during rogue suit selection */
    if (flow_step == FLOW_ROGUE_SUIT_CHOOSING) {
        rs->suit_hover_idx = -1;
        Vector2 mpos = is->mouse_pos;
        for (int s = 0; s < SUIT_COUNT; s++) {
            if (CheckCollisionPointRec(mpos, rs->suit_indicator_rects[s])) {
                rs->suit_hover_idx = s;
                break;
            }
        }
    }

    /* Generate game-layer commands from mouse clicks */
    if (is->pressed[INPUT_ACTION_LEFT_CLICK]) {
        Vector2 mouse = is->mouse_pos;

        /* Pause menu button clicks — intercept before game switch */
        if (rs->pause_state == PAUSE_MENU && is_ingame_phase(gs->phase)) {
            /* Continue */
            if (render_hit_test_button(&rs->pause_btns[0], mouse)) {
                rs->pause_state = PAUSE_INACTIVE;
            }
            /* Settings */
            else if (render_hit_test_button(&rs->pause_btns[1], mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_OPEN_SETTINGS,
                    .source_player = 0,
                });
            }
            /* Return to Menu */
            else if (render_hit_test_button(&rs->pause_btns[2], mouse)) {
                rs->pause_state = PAUSE_CONFIRM_MENU;
            }
            /* Quit Game */
            else if (render_hit_test_button(&rs->pause_btns[3], mouse)) {
                rs->pause_state = PAUSE_CONFIRM_QUIT;
            }
            goto skip_game_clicks;
        }

        /* Confirmation dialog clicks */
        if ((rs->pause_state == PAUSE_CONFIRM_MENU ||
             rs->pause_state == PAUSE_CONFIRM_QUIT) &&
            is_ingame_phase(gs->phase)) {
            if (render_hit_test_button(&rs->pause_confirm_yes, mouse)) {
                if (rs->pause_state == PAUSE_CONFIRM_MENU) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_RETURN_TO_MENU,
                        .source_player = 0,
                    });
                } else {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_QUIT,
                        .source_player = 0,
                    });
                }
            } else if (render_hit_test_button(&rs->pause_confirm_no, mouse)) {
                rs->pause_state = PAUSE_MENU;
            }
            goto skip_game_clicks;
        }

        /* Transmutation inventory drag: available in any phase for reorder.
         * Selection/apply gated to PASS_SUB_CARD_PASS in the release handler. */
        if (p2->enabled && rs->transmute_btn_count > 0) {
            int tmut_hit = render_hit_test_transmute(rs, mouse);
            if (tmut_hit >= 0) {
                render_start_transmute_drag(rs, tmut_hit, mouse);
                goto skip_game_clicks;
            }
        }

        /* Universal hand card drag (rearrange only) for phases that don't
         * have their own card drag logic. PHASE_PASSING (card pass) and
         * PHASE_PLAYING have specialized handlers inside the switch. */
        if (gs->phase != PHASE_PASSING && gs->phase != PHASE_PLAYING) {
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
                    render_start_card_drag(rs, hit, hand_slot, mouse, false);
                    goto skip_game_clicks;
                }
            }
        }

        switch (gs->phase) {
        case PHASE_LOGIN:
            if (render_hit_test_button(&rs->btn_login_submit, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_LOGIN_SUBMIT,
                    .source_player = 0,
                });
            } else if (render_hit_test_button(&rs->btn_login_retry, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_LOGIN_RETRY,
                    .source_player = 0,
                });
            } else if (render_hit_test_button(&rs->btn_login_import, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_IDENTITY_IMPORT,
                    .source_player = 0,
                });
            } else if (render_hit_test_button(&rs->btn_login_refresh, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_IDENTITY_REFRESH,
                    .source_player = 0,
                });
            }
            break;

        case PHASE_ONLINE_MENU:
            /* Sub-menu buttons */
            for (int i = 0; i < rs->online_btn_count; i++) {
                if (render_hit_test_button(&rs->online_btns[i], mouse)) {
                    /* When 5 buttons: 0=Reconnect,1=Quick,2=Create,3=Join,4=Back
                     * When 4 buttons: 0=Quick,1=Create,2=Join,3=Back */
                    InputCmdType types5[] = {
                        INPUT_CMD_ONLINE_RECONNECT,
                        INPUT_CMD_ONLINE_QUICKMATCH,
                        INPUT_CMD_ONLINE_CREATE,
                        INPUT_CMD_ONLINE_JOIN,
                        INPUT_CMD_ONLINE_CANCEL,
                    };
                    InputCmdType types4[] = {
                        INPUT_CMD_ONLINE_QUICKMATCH,
                        INPUT_CMD_ONLINE_CREATE,
                        INPUT_CMD_ONLINE_JOIN,
                        INPUT_CMD_ONLINE_CANCEL,
                    };
                    InputCmdType type = (rs->online_btn_count == 5)
                        ? types5[i] : types4[i];
                    input_cmd_push((InputCmd){
                        .type = type,
                        .source_player = 0,
                    });
                    break;
                }
            }
            /* Join submit */
            if (render_hit_test_button(&rs->btn_online_join_submit, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_ONLINE_JOIN,
                    .source_player = 0,
                });
            }
            /* AI difficulty arrows — both cycle the same toggle */
            if (render_hit_test_button(&rs->btn_online_ai_diff_prev, mouse) ||
                render_hit_test_button(&rs->btn_online_ai_diff_next, mouse)) {
                OnlineUIState *oui = (OnlineUIState *)rs->online_ui;
                if (oui) oui->ai_difficulty = !oui->ai_difficulty;
            }
            /* Game option arrows */
            {
                OnlineUIState *oui = (OnlineUIState *)rs->online_ui;
                if (oui) {
                    if (render_hit_test_button(&rs->btn_opt_timer_prev, mouse))
                        oui->timer_option = (oui->timer_option - 1 + TIMER_OPTION_COUNT) % TIMER_OPTION_COUNT;
                    if (render_hit_test_button(&rs->btn_opt_timer_next, mouse))
                        oui->timer_option = (oui->timer_option + 1) % TIMER_OPTION_COUNT;
                    if (render_hit_test_button(&rs->btn_opt_points_prev, mouse))
                        oui->point_goal = (oui->point_goal - 1 + POINT_GOAL_COUNT) % POINT_GOAL_COUNT;
                    if (render_hit_test_button(&rs->btn_opt_points_next, mouse))
                        oui->point_goal = (oui->point_goal + 1) % POINT_GOAL_COUNT;
                    if (render_hit_test_button(&rs->btn_opt_mode_prev, mouse))
                        oui->gamemode = (oui->gamemode - 1 + GAMEMODE_COUNT) % GAMEMODE_COUNT;
                    if (render_hit_test_button(&rs->btn_opt_mode_next, mouse))
                        oui->gamemode = (oui->gamemode + 1) % GAMEMODE_COUNT;
                }
            }
            /* Add AI to waiting room */
            if (render_hit_test_button(&rs->btn_online_add_ai, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_ONLINE_ADD_AI,
                    .source_player = 0,
                });
            }
            /* Remove AI from waiting room */
            if (render_hit_test_button(&rs->btn_online_remove_ai, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_ONLINE_REMOVE_AI,
                    .source_player = 0,
                });
            }
            /* Start game (room creator) */
            if (render_hit_test_button(&rs->btn_online_start_game, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_ONLINE_START,
                    .source_player = 0,
                });
            }
            /* Try Again from error screen */
            if (render_hit_test_button(&rs->btn_online_try_again, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_ONLINE_JOIN,
                    .source_player = 0,
                });
            }
            /* Cancel from sub-states */
            if (render_hit_test_button(&rs->btn_online_cancel, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_ONLINE_CANCEL,
                    .source_player = 0,
                });
            }
            break;

        case PHASE_MENU:
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                if (render_hit_test_button(&rs->menu_items[i], mouse)) {
                    switch ((MenuItem)i) {
                    case MENU_PLAY:
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_OPEN_PLAY,
                            .source_player = 0,
                        });
                        break;
                    case MENU_STATISTICS:
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_OPEN_STATS,
                            .source_player = 0,
                        });
                        break;
                    case MENU_ACHIEVEMENTS:
                        break; /* disabled — coming soon */
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
            if (pps->subphase == PASS_SUB_DEALER && rs->dealer_ui_active) {
                /* Direction buttons */
                static const int dir_values[] = {PASS_LEFT, PASS_ACROSS, PASS_RIGHT};
                for (int i = 0; i < DEALER_DIR_BTN_COUNT; i++) {
                    if (rs->dealer_dir_btns[i].visible &&
                        CheckCollisionPointRec(mouse, rs->dealer_dir_btns[i].bounds)) {
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_DEALER_DIR,
                            .source_player = 0,
                            .dealer_dir = { .direction = dir_values[i] },
                        });
                        break;
                    }
                }
                /* Amount buttons */
                for (int i = 0; i < DEALER_AMT_BTN_COUNT; i++) {
                    if (rs->dealer_amt_btns[i].visible &&
                        CheckCollisionPointRec(mouse, rs->dealer_amt_btns[i].bounds)) {
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_DEALER_AMT,
                            .source_player = 0,
                            .dealer_amt = { .amount = DEALER_AMOUNTS[i] },
                        });
                        break;
                    }
                }
                /* Confirm button */
                if (rs->dealer_confirm_btn.visible &&
                    CheckCollisionPointRec(mouse, rs->dealer_confirm_btn.bounds)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_DEALER_CONFIRM,
                        .source_player = 0,
                    });
                }
            } else if (pps->subphase == PASS_SUB_CONTRACT &&
                       !pps->draft_pick_pending &&
                       !pps->draft_click_consumed) {
                int contract_hit = render_hit_test_contract(rs, mouse);
                if (contract_hit >= 0) {
                    pps->draft_click_consumed = true;
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SELECT_CONTRACT,
                        .source_player = 0,
                        .contract = { .pair_index = contract_hit },
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
                                .transmute_apply = {
                                    .hand_index = hand_idx,
                                    .card = gs->players[0].hand.cards[hand_idx],
                                },
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
            /* Fallback: rearrange-only card drag during any pass subphase
             * that doesn't have its own card interaction (dealer, draft, anims). */
            if (pps->subphase != PASS_SUB_CARD_PASS) {
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
                        render_start_card_drag(rs, hit, hand_slot, mouse, false);
                    }
                }
            }
            break;
        }

        case PHASE_PLAYING: {
            /* Duel pick opponent: intercept clicks on opponent name indicators */
            if (flow_step == FLOW_DUEL_PICK_OPPONENT) {
                int opp_player = -1;
                for (int p = 1; p < NUM_PLAYERS; p++) {
                    if (CheckCollisionPointRec(mouse, rs->opponent_indicator_rects[p])) {
                        opp_player = p;
                        break;
                    }
                }
                if (opp_player > 0) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_DUEL_PICK,
                        .source_player = 0,
                        .duel_pick = { .target_player = opp_player },
                    });
                }
                break;
            }
            /* Duel pick own card or return: intercept clicks (not when watching) */
            if (flow_step == FLOW_DUEL_PICK_OWN && !rs->duel_watching) {
                /* Check own hand cards first */
                int hit = render_hit_test_card(rs, mouse);
                if (hit >= 0) {
                    int hand_idx = -1;
                    for (int ci = 0; ci < rs->hand_visual_counts[0]; ci++) {
                        if (rs->hand_visuals[0][ci] == hit) {
                            hand_idx = ci;
                            break;
                        }
                    }
                    if (hand_idx >= 0) {
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_DUEL_GIVE,
                            .source_player = 0,
                            .duel_give = { .hand_index = hand_idx },
                        });
                    }
                    break;
                }
                /* Check if clicking the same opponent card to return it */
                int opp_player = -1;
                int cv_hit = render_hit_test_opponent_card(rs, mouse, &opp_player);
                if (cv_hit >= 0 && opp_player > 0) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_DUEL_RETURN,
                        .source_player = 0,
                    });
                }
                break;
            }
            /* Rogue choosing: intercept clicks on opponent name indicators */
            if (flow_step == FLOW_ROGUE_CHOOSING) {
                int opp_player = -1;
                for (int p = 1; p < NUM_PLAYERS; p++) {
                    if (CheckCollisionPointRec(mouse, rs->opponent_indicator_rects[p])) {
                        opp_player = p;
                        break;
                    }
                }
                if (opp_player > 0) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_ROGUE_PICK,
                        .source_player = 0,
                        .rogue_pick = { .target_player = opp_player },
                    });
                }
                break;
            }
            /* Rogue suit choosing: intercept clicks on suit windows */
            if (flow_step == FLOW_ROGUE_SUIT_CHOOSING) {
                for (int s = 0; s < SUIT_COUNT; s++) {
                    if (CheckCollisionPointRec(mouse, rs->suit_indicator_rects[s])) {
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_ROGUE_REVEAL,
                            .source_player = 0,
                            .rogue_reveal = {
                                .target_player = rs->rogue_target_player,
                                .suit = s,
                            },
                        });
                        break;
                    }
                }
                break;
            }
            /* Card interaction */
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
                        bool play_drag = is_human_turn && rs->card_playable[hand_slot];
                        render_start_card_drag(rs, hit, hand_slot, mouse, play_drag);
                    }
                }
            }
            break;
        }

        case PHASE_STATS: {
            /* Tab switching */
            for (int t = 0; t < STATS_TAB_COUNT; t++) {
                if (render_hit_test_button(&rs->stats_tab_btns[t], mouse)) {
                    if (rs->stats_tab != (StatsTab)t) {
                        rs->stats_tab = (StatsTab)t;
                        if (t == STATS_TAB_LEADERBOARDS &&
                            !rs->leaderboard_loaded &&
                            !rs->leaderboard_loading &&
                            lobby_client_state() == LOBBY_AUTHENTICATED) {
                            rs->leaderboard_loading = true;
                            rs->leaderboard_scroll_y = 0.0f;
                            lobby_client_request_leaderboard();
                        }
                    }
                    break;
                }
            }
            if (render_hit_test_button(&rs->btn_stats_back, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_CANCEL,
                    .source_player = 0,
                });
            }
            break;
        }

        case PHASE_SETTINGS: {
            /* Tab switching */
            bool tab_clicked = false;
            for (int t = 0; t < SETTINGS_TAB_COUNT; t++) {
                if (render_hit_test_button(&rs->settings_tab_btns[t], mouse)) {
                    rs->settings_tab = (SettingsTab)t;
                    tab_clicked = true;
                    break;
                }
            }
            if (tab_clicked) break;
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
            /* Account tab buttons */
            if (render_hit_test_button(&rs->btn_account_export, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_IDENTITY_EXPORT,
                    .source_player = 0,
                });
            } else if (render_hit_test_button(&rs->btn_account_import, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_IDENTITY_IMPORT,
                    .source_player = 0,
                });
            } else if (render_hit_test_button(&rs->btn_account_refresh, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_IDENTITY_REFRESH,
                    .source_player = 0,
                });
            } else if (render_hit_test_button(&rs->btn_account_confirm_yes, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_IDENTITY_IMPORT_CONFIRM,
                    .source_player = 0,
                });
            } else if (render_hit_test_button(&rs->btn_account_confirm_no, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_IDENTITY_IMPORT_CANCEL,
                    .source_player = 0,
                });
            }
            break;
        }

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

    skip_game_clicks: (void)0;
    }

}
