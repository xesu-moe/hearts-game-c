/* ============================================================
 * @deps-implements: phase_transitions.h
 * @deps-requires: phase_transitions.h, core/game_state.h, render/render.h,
 *                 render/anim.h, render/particle.h,
 *                 game/pass_phase.h, game/play_phase.h, game/turn_flow.h,
 *                 phase2/phase2_state.h, phase2/contract_logic.h,
 *                 phase2/vendetta_logic.h
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "phase_transitions.h"

#include <stdio.h>

#include "render/render.h"
#include "render/anim.h"
#include "render/particle.h"
#include "phase2/contract_logic.h"
#include "phase2/vendetta_logic.h"

void phase_transition_update(GameState *gs, RenderState *rs,
                             Phase2State *p2, PassPhaseState *pps,
                             PlayPhaseState *pls, TurnFlow *flow,
                             GamePhase *prev_phase,
                             bool *prev_hearts_broken)
{
    /* Deal animation complete — advance to next phase */
    if (gs->phase == PHASE_DEALING && rs->deal_complete) {
        gs->phase = (gs->pass_direction == PASS_NONE)
                       ? PHASE_PLAYING : PHASE_PASSING;
        rs->sync_needed = true;
        rs->deal_complete = false;
    }

    /* Reset flow when entering PHASE_PLAYING */
    if (gs->phase == PHASE_PLAYING && *prev_phase != PHASE_PLAYING) {
        flow_init(flow);
        rs->sync_needed = true;
        for (int ti = 0; ti < CARDS_PER_TRICK; ti++)
            pls->current_tti.transmutation_ids[ti] = -1;
    }

    /* Set up Phase 2 subphases when entering PHASE_PASSING */
    if (gs->phase == PHASE_PASSING && *prev_phase != PHASE_PASSING) {
        pps->timer = 0.0f;
        pps->ai_vendetta_pending = false;
        pps->vendetta_ui_active = false;

        if (p2->enabled) {
            contract_round_reset(p2);
            vendetta_round_reset(p2);
            p2->round.vendetta_player_id =
                vendetta_determine_player(p2->round.prev_round_points,
                                          gs->round_number);

            int vid = p2->round.vendetta_player_id;
            if (vid >= 0 &&
                vendetta_has_options(p2, VENDETTA_TIMING_PASSING)) {
                advance_pass_subphase(pps, gs, rs, p2, PASS_SUB_VENDETTA);
            } else {
                advance_pass_subphase(pps, gs, rs, p2, PASS_SUB_CONTRACT);
            }
        } else {
            pps->subphase = PASS_SUB_CARD_PASS;
            rs->pass_subphase = PASS_SUB_CARD_PASS;
            rs->pass_subphase_remaining = PASS_CARD_PASS_TIME;
            rs->pass_status_text = NULL;
            rs->contract_ui_active = false;

            if (gs->pass_direction == PASS_NONE) {
                gs->phase = PHASE_PLAYING;
                rs->sync_needed = true;
            }
        }
    }

    /* Chat log: round start */
    if (gs->phase == PHASE_PASSING && *prev_phase != PHASE_PASSING) {
        char msg[CHAT_MSG_LEN];
        snprintf(msg, sizeof(msg), "-- Round %d --", gs->round_number);
        render_chat_log_push(rs, msg);
    }

    /* Chat log: hearts broken */
    if (gs->hearts_broken && !*prev_hearts_broken) {
        render_chat_log_push_color(rs, "Hearts Broken!", RED);
    }

    *prev_phase = gs->phase;
}

void phase_transition_post_render(GameState *gs, RenderState *rs,
                                  bool *prev_hearts_broken)
{
    /* Particle burst: hearts broken (after render_update so trick_visuals are synced) */
    if (gs->hearts_broken && !*prev_hearts_broken) {
        for (int ti = 0; ti < rs->trick_visual_count; ti++) {
            int idx = rs->trick_visuals[ti];
            if (idx < 0 || idx >= rs->card_count) continue;
            if (rs->cards[idx].card.suit != SUIT_HEARTS) continue;
            CardVisual *cv = &rs->cards[idx];
            Vector2 pos = cv->animating ? cv->target : cv->position;
            Vector2 center = {
                pos.x + rs->layout.card_width * 0.5f,
                pos.y + rs->layout.card_height * 0.5f
            };
            particle_spawn_burst(&rs->particles, center, 48);
            break;
        }
    }
    *prev_hearts_broken = gs->hearts_broken;
}
