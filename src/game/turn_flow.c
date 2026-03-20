/* ============================================================
 * @deps-implements: turn_flow.h
 * @deps-requires: turn_flow.h, core/game_state.h (game_state_*,
 *                 game_state_complete_trick_with), core/settings.h,
 *                 core/trick.h, game/ai.h (ai_duel_choose, ai_duel_execute),
 *                 render/render.h (opponent_hover_active, render_chat_log_push),
 *                 render/layout.h (layout_board_center, layout_pile_position),
 *                 render/anim.h (ANIM_EFFECT_FLIGHT_DURATION,
 *                 ANIM_DUEL_EXCHANGE_DURATION), game/play_phase.h,
 *                 phase2 (transmute, contract, vendetta)
 * @deps-used-by: process_input.c, phase_transitions.c, update.c, main.c
 * @deps-last-changed: 2026-03-20 — Added try_start_duel(), uses new
 *                     animation constants, FlowStep values, ai_duel_choose
 * ============================================================ */

#include "turn_flow.h"

#include <stdio.h>

#include <stdlib.h>

#include "ai.h"
#include "core/trick.h"
#include "render/render.h"
#include "render/layout.h"
#include "phase2/contract_logic.h"
#include "phase2/vendetta_logic.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

/* Map player_id to screen position (matches render.c player_screen_pos) */
static const PlayerPosition pos_map[NUM_PLAYERS] = {
    POS_BOTTOM, POS_LEFT, POS_TOP, POS_RIGHT
};

/* INVARIANT: Do NOT set sync_needed between FLOW_ROGUE_CHOOSING and
 * FLOW_ROGUE_ANIM_BACK, or between FLOW_DUEL_PICK_OPPONENT and
 * FLOW_DUEL_ANIM_EXCHANGE/RETURN. Staged cv_idx references would be
 * invalidated by sync_hands() rebuilding the card pool. */

/* Launch a Rogue card flight from hand to board center.
 * Sets face_up, z_order, starts animation, and transitions to ANIM_TO_CENTER. */
static void rogue_launch_flight(TurnFlow *flow, RenderState *rs,
                                int rp, int ri, const char *msg,
                                float anim_m)
{
    if (ri < rs->hand_visual_counts[rp]) {
        int cv_idx = rs->hand_visuals[rp][ri];
        if (cv_idx >= 0 && cv_idx < rs->card_count) {
            rs->cards[cv_idx].face_up = true;
            rs->cards[cv_idx].z_order = 200;
            anim_start(&rs->cards[cv_idx],
                       layout_board_center(&rs->layout), 0.0f,
                       ANIM_EFFECT_FLIGHT_DURATION * anim_m,
                       EASE_IN_OUT_QUAD);
            flow->rogue_staged_cv_idx = cv_idx;
        }
        render_chat_log_push(rs, msg);
    }
    rs->opponent_hover_active = false;
    flow->step = FLOW_ROGUE_ANIM_TO_CENTER;
    flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
}

/* Try to start the Duel effect. Returns true if flow was redirected. */
static bool try_start_duel(TurnFlow *flow, GameState *gs, RenderState *rs,
                           Phase2State *p2, GameSettings *settings)
{
    if (!p2->enabled || gs->phase != PHASE_PLAYING ||
        p2->round.transmute_round.duel_pending_winner < 0)
        return false;

    float anim_m = settings_anim_multiplier(settings->anim_speed);
    int dw = p2->round.transmute_round.duel_pending_winner;
    p2->round.transmute_round.duel_pending_winner = -1;
    flow->duel_winner = dw;
    flow->duel_target_player = -1;
    flow->duel_target_card_idx = -1;
    flow->duel_own_card_idx = -1;
    flow->duel_returned = false;
    flow->duel_staged_cv_idx = -1;
    flow->duel_own_cv_idx = -1;
    flow->duel_ai_decided = false;

    if (dw == 0) {
        /* Human winner: wait for click with hover */
        flow->step = FLOW_DUEL_PICK_OPPONENT;
        flow->timer = FLOW_DUEL_CHOOSE_TIME;
        rs->opponent_hover_active = true;
        render_chat_log_push(rs, "Duel: Choose an opponent's card to take!");
        rs->sync_needed = true;
    } else {
        /* AI winner: decide targets, animate to center */
        int tp = -1, ti = -1, oi = -1;
        ai_duel_choose(gs, dw, &tp, &ti, &oi);
        if (tp < 0 || ti < 0 || oi < 0) {
            /* No valid targets, skip to between-tricks */
            flow->duel_winner = -1;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = FLOW_BETWEEN_TRICKS_TIME *
                          settings_anim_multiplier(settings->anim_speed);
            return true;
        }
        flow->duel_target_player = tp;
        flow->duel_target_card_idx = ti;
        flow->duel_own_card_idx = oi;
        flow->duel_ai_decided = true;

        if (ti < rs->hand_visual_counts[tp]) {
            int cv_idx = rs->hand_visuals[tp][ti];
            if (cv_idx >= 0 && cv_idx < rs->card_count) {
                rs->cards[cv_idx].revealed_to =
                    (uint8_t)((1 << dw) | (1 << tp));
                rs->cards[cv_idx].z_order = 200;
                anim_start(&rs->cards[cv_idx],
                           layout_board_center(&rs->layout), 0.0f,
                           ANIM_EFFECT_FLIGHT_DURATION * anim_m,
                           EASE_IN_OUT_QUAD);
                flow->duel_staged_cv_idx = cv_idx;
            }
        }
        char msg[CHAT_MSG_LEN];
        snprintf(msg, sizeof(msg), "Duel: %s challenges %s!",
                 p2_player_name(dw), p2_player_name(tp));
        render_chat_log_push(rs, msg);
        flow->step = FLOW_DUEL_ANIM_TO_CENTER;
        flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
    }
    return true;
}

void flow_init(TurnFlow *flow)
{
    flow->step = FLOW_IDLE;
    flow->timer = 0.0f;
    flow->turn_timer = FLOW_TURN_TIME_LIMIT;
    flow->animating_player = -1;
    flow->prev_trick_count = 0;
    flow->hearts_broken_at_trick_start = false;
    flow->rogue_winner = -1;
    flow->rogue_reveal_player = -1;
    flow->rogue_reveal_card_idx = -1;
    flow->duel_winner = -1;
    flow->duel_target_player = -1;
    flow->duel_target_card_idx = -1;
    flow->duel_own_card_idx = -1;
    flow->duel_returned = false;
    flow->rogue_staged_cv_idx = -1;
    flow->duel_staged_cv_idx = -1;
    flow->duel_own_cv_idx = -1;
    flow->duel_ai_decided = false;
}

void flow_update(TurnFlow *flow, GameState *gs, RenderState *rs,
                 Phase2State *p2, GameSettings *settings,
                 PlayPhaseState *pps, float dt)
{
    if (gs->phase != PHASE_PLAYING) {
        flow->step = FLOW_IDLE;
        return;
    }

    flow->timer -= dt;

    switch (flow->step) {
    case FLOW_IDLE: {
        float anim_mult = settings_anim_multiplier(settings->anim_speed);
        if (trick_is_complete(&gs->current_trick)) {
            flow->step = FLOW_TRICK_DISPLAY;
            flow->timer = FLOW_TRICK_DISPLAY_TIME * anim_mult;
            rs->last_trick_winner = -1;
            /* Clear fog on trick cards to reveal identity */
            for (int i = 0; i < rs->trick_visual_count; i++) {
                int idx = rs->trick_visuals[i];
                if (idx >= 0 && idx < rs->card_count) {
                    rs->cards[idx].fog_mode = 0;
                    rs->cards[idx].fog_reveal_t = 0.0f;
                }
            }
            return;
        }

        /* Snapshot hearts_broken at the start of each new trick */
        if (gs->current_trick.num_played == 0)
            flow->hearts_broken_at_trick_start = gs->hearts_broken;

        {
            int current = game_state_current_player(gs);
            if (current == 0) {
                flow->step = FLOW_WAITING_FOR_HUMAN;
                flow->prev_trick_count = gs->current_trick.num_played;
                flow->turn_timer = FLOW_TURN_TIME_LIMIT;
            } else if (current > 0) {
                flow->step = FLOW_AI_THINKING;
                flow->timer = settings_ai_think_time(settings->ai_speed);
                flow->turn_timer = FLOW_TURN_TIME_LIMIT;
            }
        }
        break;
    }

    case FLOW_WAITING_FOR_HUMAN: {
        float anim_mult = settings_anim_multiplier(settings->anim_speed);
        flow->turn_timer -= dt;

        if (flow->turn_timer <= 0.0f) {
            render_cancel_drag(rs);
            ai_play_card(gs, rs, p2, pps, 0);
        }

        if (gs->current_trick.num_played > flow->prev_trick_count) {
            rs->sync_needed = true;
            rs->anim_play_player = 0;
            flow->step = FLOW_CARD_ANIMATING;
            flow->timer = FLOW_CARD_ANIM_TIME * anim_mult;
        }
        break;
    }

    case FLOW_AI_THINKING: {
        float anim_mult = settings_anim_multiplier(settings->anim_speed);
        flow->turn_timer -= dt;
        if (flow->timer <= 0.0f) {
            int current = game_state_current_player(gs);
            if (current > 0) {
                if (p2->enabled && current == p2->round.vendetta_player_id &&
                    !p2->round.vendetta_used) {
                    vendetta_ai_activate(p2, VENDETTA_TIMING_PLAYING);
                }
                ai_play_card(gs, rs, p2, pps, current);
                rs->sync_needed = true;
                rs->anim_play_player = current;
                flow->step = FLOW_CARD_ANIMATING;
                flow->timer = FLOW_CARD_ANIM_TIME * anim_mult;
            } else {
                flow->step = FLOW_IDLE;
            }
        }
        break;
    }

    case FLOW_CARD_ANIMATING:
        if (flow->timer <= 0.0f) {
            flow->step = FLOW_IDLE;
        }
        break;

    case FLOW_TRICK_DISPLAY: {
        float anim_mult = settings_anim_multiplier(settings->anim_speed);
        if (flow->timer <= 0.0f) {
            /* Determine winner before transitioning (trick data still valid) */
            int winner;
            if (p2->enabled && trick_is_complete(&gs->current_trick)) {
                winner = transmute_trick_get_winner(&gs->current_trick,
                                                    &pps->current_tti);
            } else {
                winner = trick_get_winner(&gs->current_trick);
            }

            /* Copy trick card visuals into pile_cards[] */
            if (winner >= 0 && rs->trick_visual_count > 0) {
                const LayoutConfig *cfg = &rs->layout;
                /* Must match player_screen_pos() in render.c */

                PlayerPosition winner_spos = pos_map[winner];
                Vector2 pile_pos = layout_pile_position(winner_spos, cfg);

                for (int i = 0; i < rs->trick_visual_count; i++) {
                    int src_idx = rs->trick_visuals[i];
                    if (src_idx < 0 || src_idx >= rs->card_count) continue;
                    if (rs->pile_card_count >= MAX_PILE_CARDS) break;

                    int pi = rs->pile_card_count;
                    CardVisual *pv = &rs->pile_cards[pi];

                    /* Copy card identity and current position from trick visual */
                    *pv = rs->cards[src_idx];
                    pv->pile_owner = winner;
                    /* Carry transmutation ID for scoring display */
                    if (p2->enabled)
                        pv->transmute_id = pps->current_tti.transmutation_ids[i];
                    pv->face_up = false;  /* instant flip; hides transmute border */
                    pv->fog_mode = 0;
                    pv->fog_reveal_t = 0.0f;
                    pv->selected = false;
                    pv->hovered = false;
                    pv->hover_t = 0.0f;
                    pv->use_bezier = false;

                    /* Start from current trick position */
                    pv->start = pv->position;
                    pv->start_rotation = pv->rotation;

                    /* Target: pile position with small random scatter */
                    float scatter_x = ((float)(rand() % 7) - 3.0f) * cfg->scale;
                    float scatter_y = ((float)(rand() % 7) - 3.0f) * cfg->scale;
                    pv->target = (Vector2){
                        pile_pos.x + scatter_x,
                        pile_pos.y + scatter_y
                    };
                    float base_rot = (winner_spos == POS_LEFT || winner_spos == POS_RIGHT)
                                     ? 0.0f : 90.0f;
                    pv->target_rotation = base_rot + (float)(rand() % 11 - 5);
                    pv->scale = 0.7f * cfg->scale;
                    pv->origin = (Vector2){
                        CARD_WIDTH_REF * pv->scale * 0.5f,
                        CARD_HEIGHT_REF * pv->scale * 0.5f
                    };
                    pv->z_order = 50 + pi;
                    pv->opacity = 1.0f;

                    anim_start(pv, pv->target, pv->target_rotation,
                               ANIM_PILE_COLLECT_DURATION, EASE_OUT_QUAD);
                    pv->anim_delay = (float)i * ANIM_PILE_STAGGER * anim_mult;

                    rs->pile_card_count++;
                }

                /* Hide trick visuals so they don't double-render */
                for (int i = 0; i < rs->trick_visual_count; i++) {
                    int idx = rs->trick_visuals[i];
                    if (idx >= 0 && idx < rs->card_count)
                        rs->cards[idx].opacity = 0.0f;
                }
            }

            rs->pile_anim_in_progress = true;
            flow->step = FLOW_TRICK_PILE_ANIM;
            flow->timer = FLOW_PILE_ANIM_TIME * anim_mult;
        }
        break;
    }

    case FLOW_TRICK_PILE_ANIM: {
        float anim_mult = settings_anim_multiplier(settings->anim_speed);
        if (flow->timer <= 0.0f) {
            rs->pile_anim_in_progress = false;
            flow->step = FLOW_TRICK_COLLECTING;
            flow->timer = FLOW_TRICK_COLLECT_TIME * anim_mult;
        }
        break;
    }

    case FLOW_TRICK_COLLECTING:
        if (flow->timer <= 0.0f) {
            if (p2->enabled && trick_is_complete(&gs->current_trick)) {
                Trick saved_trick = gs->current_trick;
                TrickTransmuteInfo saved_tti = pps->current_tti;
                int winner = transmute_trick_get_winner(&saved_trick, &saved_tti);
                int points = transmute_trick_count_points(&saved_trick, &saved_tti);
                if (!game_state_complete_trick_with(gs, winner, points)) break;
                if (winner >= 0) {
                    char msg[CHAT_MSG_LEN];
                    snprintf(msg, sizeof(msg), "%s took trick %d",
                             p2_player_name(winner),
                             gs->tricks_played);
                    render_chat_log_push(rs, msg);

                    contract_on_trick_complete(p2, &saved_trick, winner,
                                               gs->tricks_played - 1,
                                               &saved_tti,
                                               flow->hearts_broken_at_trick_start);
                    transmute_on_trick_complete(p2, &saved_trick, winner,
                                                &saved_tti);
                }
                /* Reset trick transmute info for next trick */
                for (int ti = 0; ti < CARDS_PER_TRICK; ti++) {
                    pps->current_tti.transmutation_ids[ti] = -1;
                    pps->current_tti.transmuter_player[ti] = -1;
                    pps->current_tti.resolved_effects[ti] = TEFFECT_NONE;
                }
            } else {
                int winner = trick_get_winner(&gs->current_trick);
                game_state_complete_trick(gs);
                if (winner >= 0) {
                    char msg[CHAT_MSG_LEN];
                    snprintf(msg, sizeof(msg), "%s took trick %d",
                             p2_player_name(winner),
                             gs->tricks_played);
                    render_chat_log_push(rs, msg);
                }
            }
            /* Rogue effect: reveal an opponent's card */
            if (p2->enabled && gs->phase == PHASE_PLAYING &&
                p2->round.transmute_round.rogue_pending_winner >= 0) {
                float anim_m = settings_anim_multiplier(settings->anim_speed);
                int rw = p2->round.transmute_round.rogue_pending_winner;
                p2->round.transmute_round.rogue_pending_winner = -1;
                flow->rogue_winner = rw;
                flow->rogue_reveal_player = -1;
                flow->rogue_reveal_card_idx = -1;
                flow->rogue_staged_cv_idx = -1;
                if (rw == 0) {
                    /* Human winner: wait for click with hover */
                    flow->step = FLOW_ROGUE_CHOOSING;
                    flow->timer = FLOW_ROGUE_CHOOSE_TIME;
                    rs->opponent_hover_active = true;
                    render_chat_log_push(rs, "Rogue: Choose an opponent's card to reveal!");
                    rs->sync_needed = true;
                    break;
                } else {
                    /* AI winner: auto-choose + animate to center */
                    int out_p = -1, out_idx = -1;
                    ai_rogue_choose(gs, rw, &out_p, &out_idx);
                    flow->rogue_reveal_player = out_p;
                    flow->rogue_reveal_card_idx = out_idx;
                    if (out_p >= 0 && out_idx >= 0 &&
                        out_idx < rs->hand_visual_counts[out_p]) {
                        int cv_idx = rs->hand_visuals[out_p][out_idx];
                        if (cv_idx >= 0 && cv_idx < rs->card_count) {
                            rs->cards[cv_idx].face_up = true;
                            rs->cards[cv_idx].z_order = 200;
                            anim_start(&rs->cards[cv_idx],
                                       layout_board_center(&rs->layout), 0.0f,
                                       ANIM_EFFECT_FLIGHT_DURATION * anim_m,
                                       EASE_IN_OUT_QUAD);
                            flow->rogue_staged_cv_idx = cv_idx;
                        }
                        char msg[CHAT_MSG_LEN];
                        snprintf(msg, sizeof(msg), "Rogue: %s reveals %s's card!",
                                 p2_player_name(rw), p2_player_name(out_p));
                        render_chat_log_push(rs, msg);
                    }
                    flow->step = FLOW_ROGUE_ANIM_TO_CENTER;
                    flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
                    break;
                }
            }
            /* Duel effect: swap cards between players */
            if (try_start_duel(flow, gs, rs, p2, settings)) break;
            /* Apply transmutation round-end effects (e.g. Martyr doubling) */
            if (p2->enabled && gs->phase == PHASE_SCORING) {
                int rp[NUM_PLAYERS], ts[NUM_PLAYERS];
                for (int i = 0; i < NUM_PLAYERS; i++) {
                    rp[i] = gs->players[i].round_points;
                    ts[i] = gs->players[i].total_score;
                }
                transmute_apply_round_end(p2, rp, ts);
                for (int i = 0; i < NUM_PLAYERS; i++) {
                    gs->players[i].round_points = rp[i];
                    gs->players[i].total_score = ts[i];
                }
            }
            rs->sync_needed = true;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = FLOW_BETWEEN_TRICKS_TIME *
                          settings_anim_multiplier(settings->anim_speed);
        }
        break;

    case FLOW_ROGUE_CHOOSING: {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        if (flow->rogue_reveal_player >= 0 && flow->rogue_reveal_card_idx >= 0) {
            int rp = flow->rogue_reveal_player;
            int ri = flow->rogue_reveal_card_idx;
            char msg[CHAT_MSG_LEN];
            snprintf(msg, sizeof(msg), "Rogue: You reveal %s's card!",
                     p2_player_name(rp));
            rogue_launch_flight(flow, rs, rp, ri, msg, anim_m);
        } else if (flow->timer <= 0.0f) {
            int out_p = -1, out_idx = -1;
            ai_rogue_choose(gs, flow->rogue_winner, &out_p, &out_idx);
            flow->rogue_reveal_player = out_p;
            flow->rogue_reveal_card_idx = out_idx;
            char msg[CHAT_MSG_LEN];
            snprintf(msg, sizeof(msg), "Rogue: Time's up! Auto-revealed %s's card.",
                     p2_player_name(out_p));
            rogue_launch_flight(flow, rs, out_p, out_idx, msg, anim_m);
        }
        break;
    }

    case FLOW_ROGUE_ANIM_TO_CENTER:
        if (flow->timer <= 0.0f) {
            flow->step = FLOW_ROGUE_REVEAL;
            flow->timer = FLOW_ROGUE_REVEAL_TIME;
        }
        break;

    case FLOW_ROGUE_REVEAL: {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        if (flow->timer <= 0.0f) {
            /* Animate card back to hand */
            int rp = flow->rogue_reveal_player;
            int ri = flow->rogue_reveal_card_idx;
            if (rp >= 0 && ri >= 0 && ri < rs->hand_visual_counts[rp]) {
                int cv_idx = rs->hand_visuals[rp][ri];
                if (cv_idx >= 0 && cv_idx < rs->card_count) {
                    rs->cards[cv_idx].face_up = false;
                    /* Compute return position */
                    Vector2 positions[MAX_HAND_SIZE];
                    float rotations[MAX_HAND_SIZE];
                    int count = 0;
                    layout_hand_positions(pos_map[rp],
                                          rs->hand_visual_counts[rp],
                                          &rs->layout, positions, rotations,
                                          &count);
                    Vector2 ret_pos = (ri < count) ? positions[ri]
                                                   : rs->cards[cv_idx].position;
                    float ret_rot = (ri < count) ? rotations[ri] : 0.0f;
                    anim_start(&rs->cards[cv_idx], ret_pos, ret_rot,
                               ANIM_EFFECT_FLIGHT_DURATION * anim_m,
                               EASE_IN_OUT_QUAD);
                }
            }
            flow->step = FLOW_ROGUE_ANIM_BACK;
            flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
        }
        break;
    }

    case FLOW_ROGUE_ANIM_BACK: {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        if (flow->timer <= 0.0f) {
            /* Reset z_order and hover_t so card returns to normal size */
            if (flow->rogue_staged_cv_idx >= 0 &&
                flow->rogue_staged_cv_idx < rs->card_count) {
                rs->cards[flow->rogue_staged_cv_idx].z_order = 0;
                rs->cards[flow->rogue_staged_cv_idx].hover_t = 0.0f;
                rs->cards[flow->rogue_staged_cv_idx].hovered = false;
            }
            flow->rogue_winner = -1;
            flow->rogue_reveal_player = -1;
            flow->rogue_reveal_card_idx = -1;
            flow->rogue_staged_cv_idx = -1;
            /* Check for pending Duel effect before going to BETWEEN_TRICKS */
            if (try_start_duel(flow, gs, rs, p2, settings)) break;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = FLOW_BETWEEN_TRICKS_TIME * anim_m;
        }
        break;
    }

    case FLOW_DUEL_PICK_OPPONENT: {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        if (flow->duel_target_player >= 0 && flow->duel_target_card_idx >= 0) {
            /* Human selected an opponent's card — animate to center */
            int dp = flow->duel_target_player;
            int di = flow->duel_target_card_idx;
            if (di < rs->hand_visual_counts[dp]) {
                int cv_idx = rs->hand_visuals[dp][di];
                if (cv_idx >= 0 && cv_idx < rs->card_count) {
                    rs->cards[cv_idx].revealed_to =
                        (uint8_t)((1 << flow->duel_winner) | (1 << dp));
                    rs->cards[cv_idx].z_order = 200;
                    anim_start(&rs->cards[cv_idx],
                               layout_board_center(&rs->layout), 0.0f,
                               ANIM_EFFECT_FLIGHT_DURATION * anim_m,
                               EASE_IN_OUT_QUAD);
                    flow->duel_staged_cv_idx = cv_idx;
                }
                char msg[CHAT_MSG_LEN];
                snprintf(msg, sizeof(msg), "Duel: You peek at %s's card!",
                         p2_player_name(dp));
                render_chat_log_push(rs, msg);
            }
            rs->opponent_hover_active = false;
            flow->step = FLOW_DUEL_ANIM_TO_CENTER;
            flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
        } else if (flow->timer <= 0.0f) {
            /* Timeout: skip effect */
            rs->opponent_hover_active = false;
            flow->duel_winner = -1;
            render_chat_log_push(rs, "Duel: Time's up!");
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = FLOW_BETWEEN_TRICKS_TIME * anim_m;
        }
        break;
    }

    case FLOW_DUEL_ANIM_TO_CENTER:
        if (flow->timer <= 0.0f) {
            if (flow->duel_ai_decided) {
                /* AI already chose — go straight to exchange animation */
                float anim_m = settings_anim_multiplier(settings->anim_speed);
                int dw = flow->duel_winner;
                int dp = flow->duel_target_player;
                int oi = flow->duel_own_card_idx;

                /* Animate staged card -> winner's hand */
                if (flow->duel_staged_cv_idx >= 0 &&
                    flow->duel_staged_cv_idx < rs->card_count) {
                    Vector2 positions[MAX_HAND_SIZE];
                    float rotations[MAX_HAND_SIZE];
                    int count = 0;
                    layout_hand_positions(pos_map[dw],
                                          rs->hand_visual_counts[dw],
                                          &rs->layout, positions, rotations,
                                          &count);
                    Vector2 dest = (count > 0) ? positions[count / 2]
                                               : layout_board_center(&rs->layout);
                    float rot = (count > 0) ? rotations[count / 2] : 0.0f;
                    anim_start(&rs->cards[flow->duel_staged_cv_idx], dest, rot,
                               ANIM_DUEL_EXCHANGE_DURATION * anim_m,
                               EASE_IN_OUT_QUAD);
                }
                /* Animate winner's card -> opponent's hand */
                if (dw >= 0 && oi >= 0 && oi < rs->hand_visual_counts[dw]) {
                    int own_cv = rs->hand_visuals[dw][oi];
                    if (own_cv >= 0 && own_cv < rs->card_count) {
                        rs->cards[own_cv].z_order = 200;
                        Vector2 positions[MAX_HAND_SIZE];
                        float rotations[MAX_HAND_SIZE];
                        int count = 0;
                        layout_hand_positions(pos_map[dp],
                                              rs->hand_visual_counts[dp],
                                              &rs->layout, positions, rotations,
                                              &count);
                        Vector2 dest = (count > 0) ? positions[count / 2]
                                                   : layout_board_center(&rs->layout);
                        float rot = (count > 0) ? rotations[count / 2] : 0.0f;
                        anim_start(&rs->cards[own_cv], dest, rot,
                                   ANIM_DUEL_EXCHANGE_DURATION * anim_m,
                                   EASE_IN_OUT_QUAD);
                        flow->duel_own_cv_idx = own_cv;
                    }
                }
                char msg[CHAT_MSG_LEN];
                snprintf(msg, sizeof(msg), "Duel: %s swaps a card with %s!",
                         p2_player_name(dw), p2_player_name(dp));
                render_chat_log_push(rs, msg);
                flow->step = FLOW_DUEL_ANIM_EXCHANGE;
                flow->timer = ANIM_DUEL_EXCHANGE_DURATION * anim_m;
            } else {
                /* Human: pick own card */
                flow->step = FLOW_DUEL_PICK_OWN;
                flow->timer = FLOW_DUEL_CHOOSE_TIME;
            }
        }
        break;

    case FLOW_DUEL_PICK_OWN: {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        if (flow->duel_returned) {
            /* Winner chose to return the card — animate back to hand */
            int dp = flow->duel_target_player;
            int di = flow->duel_target_card_idx;
            if (flow->duel_staged_cv_idx >= 0 &&
                flow->duel_staged_cv_idx < rs->card_count) {

                Vector2 positions[MAX_HAND_SIZE];
                float rotations[MAX_HAND_SIZE];
                int count = 0;
                if (dp >= 0)
                    layout_hand_positions(pos_map[dp],
                                          rs->hand_visual_counts[dp],
                                          &rs->layout, positions, rotations,
                                          &count);
                Vector2 dest = (di >= 0 && di < count)
                    ? positions[di]
                    : rs->cards[flow->duel_staged_cv_idx].position;
                float rot = (di >= 0 && di < count) ? rotations[di] : 0.0f;
                anim_start(&rs->cards[flow->duel_staged_cv_idx], dest, rot,
                           ANIM_EFFECT_FLIGHT_DURATION * anim_m,
                           EASE_IN_OUT_QUAD);
            }
            render_chat_log_push(rs, "Duel: Card returned.");
            flow->step = FLOW_DUEL_ANIM_RETURN;
            flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
        } else if (flow->duel_own_card_idx >= 0) {
            /* Exchange: animate both cards simultaneously */
            int dw = flow->duel_winner;
            int dp = flow->duel_target_player;
            int oi = flow->duel_own_card_idx;
            /* Staged card (at center) -> winner's hand */
            if (flow->duel_staged_cv_idx >= 0 &&
                flow->duel_staged_cv_idx < rs->card_count) {
                Vector2 positions[MAX_HAND_SIZE];
                float rotations[MAX_HAND_SIZE];
                int count = 0;
                layout_hand_positions(pos_map[dw],
                                      rs->hand_visual_counts[dw],
                                      &rs->layout, positions, rotations,
                                      &count);
                Vector2 dest = (count > 0) ? positions[count / 2]
                                           : layout_board_center(&rs->layout);
                float rot = (count > 0) ? rotations[count / 2] : 0.0f;
                anim_start(&rs->cards[flow->duel_staged_cv_idx], dest, rot,
                           ANIM_DUEL_EXCHANGE_DURATION * anim_m,
                           EASE_IN_OUT_QUAD);
            }
            /* Winner's own card -> opponent's hand */
            if (dw >= 0 && oi >= 0 && oi < rs->hand_visual_counts[dw]) {
                int own_cv = rs->hand_visuals[dw][oi];
                if (own_cv >= 0 && own_cv < rs->card_count) {
                    rs->cards[own_cv].z_order = 200;
                    Vector2 positions[MAX_HAND_SIZE];
                    float rotations[MAX_HAND_SIZE];
                    int count = 0;
                    layout_hand_positions(pos_map[dp],
                                          rs->hand_visual_counts[dp],
                                          &rs->layout, positions, rotations,
                                          &count);
                    Vector2 dest = (count > 0) ? positions[count / 2]
                                               : layout_board_center(&rs->layout);
                    float rot = (count > 0) ? rotations[count / 2] : 0.0f;
                    anim_start(&rs->cards[own_cv], dest, rot,
                               ANIM_DUEL_EXCHANGE_DURATION * anim_m,
                               EASE_IN_OUT_QUAD);
                    flow->duel_own_cv_idx = own_cv;
                }
            }
            render_chat_log_push(rs, "Duel: Cards exchanged!");
            flow->step = FLOW_DUEL_ANIM_EXCHANGE;
            flow->timer = ANIM_DUEL_EXCHANGE_DURATION * anim_m;
        } else if (flow->timer <= 0.0f) {
            /* Timeout: return card */
            if (flow->duel_staged_cv_idx >= 0 &&
                flow->duel_staged_cv_idx < rs->card_count) {
                int dp = flow->duel_target_player;
                int di = flow->duel_target_card_idx;

                Vector2 positions[MAX_HAND_SIZE];
                float rotations[MAX_HAND_SIZE];
                int count = 0;
                if (dp >= 0)
                    layout_hand_positions(pos_map[dp],
                                          rs->hand_visual_counts[dp],
                                          &rs->layout, positions, rotations,
                                          &count);
                Vector2 dest = (di >= 0 && di < count)
                    ? positions[di]
                    : rs->cards[flow->duel_staged_cv_idx].position;
                float rot = (di >= 0 && di < count) ? rotations[di] : 0.0f;
                anim_start(&rs->cards[flow->duel_staged_cv_idx], dest, rot,
                           ANIM_EFFECT_FLIGHT_DURATION * anim_m,
                           EASE_IN_OUT_QUAD);
            }
            render_chat_log_push(rs, "Duel: Time's up! Card returned.");
            flow->step = FLOW_DUEL_ANIM_RETURN;
            flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
        }
        break;
    }

    case FLOW_DUEL_ANIM_EXCHANGE: {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        if (flow->timer <= 0.0f) {
            /* Clear revealed_to on staged card */
            if (flow->duel_staged_cv_idx >= 0 &&
                flow->duel_staged_cv_idx < rs->card_count)
                rs->cards[flow->duel_staged_cv_idx].revealed_to = 0;
            /* Execute swap AFTER animation */
            transmute_swap_between_players(gs, p2,
                flow->duel_winner, flow->duel_own_card_idx,
                flow->duel_target_player, flow->duel_target_card_idx);
            rs->sync_needed = true;
            /* Reset z_orders */
            if (flow->duel_staged_cv_idx >= 0 &&
                flow->duel_staged_cv_idx < rs->card_count)
                rs->cards[flow->duel_staged_cv_idx].z_order = 0;
            if (flow->duel_own_cv_idx >= 0 &&
                flow->duel_own_cv_idx < rs->card_count)
                rs->cards[flow->duel_own_cv_idx].z_order = 0;
            /* Clear fields */
            flow->duel_winner = -1;
            flow->duel_target_player = -1;
            flow->duel_target_card_idx = -1;
            flow->duel_own_card_idx = -1;
            flow->duel_returned = false;
            flow->duel_staged_cv_idx = -1;
            flow->duel_own_cv_idx = -1;
            flow->duel_ai_decided = false;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = FLOW_BETWEEN_TRICKS_TIME * anim_m;
        }
        break;
    }

    case FLOW_DUEL_ANIM_RETURN: {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        if (flow->timer <= 0.0f) {
            /* Clear revealed_to */
            if (flow->duel_staged_cv_idx >= 0 &&
                flow->duel_staged_cv_idx < rs->card_count) {
                rs->cards[flow->duel_staged_cv_idx].revealed_to = 0;
                rs->cards[flow->duel_staged_cv_idx].z_order = 0;
            }
            /* Clear fields */
            flow->duel_winner = -1;
            flow->duel_target_player = -1;
            flow->duel_target_card_idx = -1;
            flow->duel_own_card_idx = -1;
            flow->duel_returned = false;
            flow->duel_staged_cv_idx = -1;
            flow->duel_own_cv_idx = -1;
            flow->duel_ai_decided = false;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = FLOW_BETWEEN_TRICKS_TIME * anim_m;
        }
        break;
    }

    case FLOW_BETWEEN_TRICKS:
        if (flow->timer <= 0.0f) {
            flow->step = FLOW_IDLE;
        }
        break;
    }
}
