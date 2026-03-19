/* ============================================================
 * @deps-implements: phase_transitions.h
 * @deps-requires: phase_transitions.h, core/game_state.h (GamePhase),
 *                 render/render.h (ScoringSubphase, layout_scoring_card_position),
 *                 render/layout.h (layout_scoring_card_position, LayoutConfig),
 *                 render/anim.h (CardVisual, ANIM_SCORING_*), render/particle.h,
 *                 game/pass_phase.h, game/play_phase.h, game/turn_flow.h,
 *                 phase2/phase2_state.h, phase2/contract_logic.h,
 *                 phase2/vendetta_logic.h, stdio.h
 * @deps-last-changed: 2026-03-19 — Added scoring animation setup with CardVisual pool
 * ============================================================ */

#include "phase_transitions.h"

#include <stdio.h>

#include "core/card.h"
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
    /* Initialize scoring animation when entering PHASE_SCORING */
    if (gs->phase == PHASE_SCORING && *prev_phase != PHASE_SCORING) {
        float anim_mult = anim_get_speed();
        rs->score_subphase = SCORE_SUB_CARDS_FLY;
        rs->score_anim_timer = 0.0f;
        rs->score_menu_slide_y = -rs->layout.board_size;
        rs->score_cards_landed = false;
        rs->score_menu_arrived = false;
        rs->score_countup_timer = 0.0f;
        rs->score_tick_pending = false;
        rs->btn_continue.visible = false;

        /* Snapshot scores: displayed_total shows pre-round total */
        for (int i = 0; i < NUM_PLAYERS; i++) {
            rs->displayed_total_scores[i] =
                gs->players[i].total_score - gs->players[i].round_points;
            rs->displayed_round_points[i] = gs->players[i].round_points;
            rs->score_countup_round[i] = gs->players[i].round_points;
        }

        /* Count scoring cards per player for stagger indexing */
        int card_idx_per_player[NUM_PLAYERS] = {0};
        const LayoutConfig *cfg = &rs->layout;

        /* Compute final table positions (slide_y=0 = fully visible) */
        ScoringTableLayout tbl;
        layout_scoring_table(cfg, 0.0f, &tbl);

        for (int i = 0; i < rs->pile_card_count; i++) {
            CardVisual *pv = &rs->pile_cards[i];
            int pts = card_points(pv->card);
            if (pts == 0) {
                /* Non-scoring card: hide instantly */
                pv->opacity = 0.0f;
            } else {
                /* Scoring card: flip face-up, shrink, fly to row */
                int owner = pv->pile_owner;
                if (owner < 0 || owner >= NUM_PLAYERS) {
                    pv->opacity = 0.0f;
                    continue;
                }
                int ci = card_idx_per_player[owner]++;

                pv->face_up = true;
                pv->start = pv->position;
                pv->start_rotation = pv->rotation;

                Vector2 target = layout_scoring_card_position(
                    owner, ci, cfg, &tbl);
                float target_scale = 0.5f * cfg->scale;
                pv->scale = target_scale;
                pv->origin = (Vector2){
                    CARD_WIDTH_REF * target_scale * 0.5f,
                    CARD_HEIGHT_REF * target_scale * 0.5f
                };
                pv->z_order = 200 + owner * 20 + ci;

                float delay = ((float)owner * ANIM_SCORING_PLAYER_STAGGER +
                               (float)ci * ANIM_SCORING_CARD_STAGGER) * anim_mult;
                anim_start(pv, target, 10.0f,
                           ANIM_SCORING_FLY_DURATION, EASE_OUT_QUAD);
                pv->anim_delay = delay;
            }
        }
    }

    /* Clear piles when entering dealing phase (new round) */
    if (gs->phase == PHASE_DEALING && *prev_phase != PHASE_DEALING) {
        render_clear_piles(rs);
    }

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
        render_clear_piles(rs);
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
