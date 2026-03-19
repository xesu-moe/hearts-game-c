/* ============================================================
 * @deps-implements: turn_flow.h
 * @deps-requires: turn_flow.h, core/game_state.h, core/settings.h,
 *                 ai.h, core/trick.h, render/render.h (CardVisual pool),
 *                 render/layout.h (layout_pile_position), render/anim.h
 *                 (CardVisual.pile_owner, ANIM_PILE_*), game/play_phase.h,
 *                 phase2/phase2_state.h, phase2/contract_logic.h,
 *                 phase2/vendetta_logic.h, phase2/transmutation_logic.h,
 *                 phase2/phase2_defs.h, stdio.h, stdlib.h
 * @deps-last-changed: 2026-03-19 — Sets pile_owner on CardVisual in pile collection
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

void flow_init(TurnFlow *flow)
{
    flow->step = FLOW_IDLE;
    flow->timer = 0.0f;
    flow->turn_timer = FLOW_TURN_TIME_LIMIT;
    flow->animating_player = -1;
    flow->prev_trick_count = 0;
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
            return;
        }

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
                static const PlayerPosition pos_map[NUM_PLAYERS] = {
                    POS_BOTTOM, POS_LEFT, POS_TOP, POS_RIGHT
                };
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
                    pv->face_up = false;  /* instant flip */
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
                game_state_complete_trick(gs);
                if (winner >= 0) {
                    char msg[CHAT_MSG_LEN];
                    snprintf(msg, sizeof(msg), "%s took trick %d",
                             p2_player_name(winner),
                             gs->tricks_played);
                    render_chat_log_push(rs, msg);

                    contract_on_trick_complete(p2, &saved_trick, winner);
                }
                /* Reset trick transmute info for next trick */
                for (int ti = 0; ti < CARDS_PER_TRICK; ti++)
                    pps->current_tti.transmutation_ids[ti] = -1;
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
            rs->sync_needed = true;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = FLOW_BETWEEN_TRICKS_TIME *
                          settings_anim_multiplier(settings->anim_speed);
        }
        break;

    case FLOW_BETWEEN_TRICKS:
        if (flow->timer <= 0.0f) {
            flow->step = FLOW_IDLE;
        }
        break;
    }
}
