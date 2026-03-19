/* ============================================================
 * @deps-implements: turn_flow.h
 * @deps-requires: turn_flow.h, core/game_state.h, core/settings.h,
 *                 core/ai.h, render/render.h, game/play_phase.h,
 *                 phase2/phase2_state.h, phase2/contract_logic.h,
 *                 phase2/vendetta_logic.h, phase2/transmutation_logic.h
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "turn_flow.h"

#include <stdio.h>

#include "core/ai.h"
#include "core/trick.h"
#include "render/render.h"
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
