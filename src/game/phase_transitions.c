/* ============================================================
 * @deps-implements: phase_transitions.h
 * @deps-requires: phase_transitions.h, core/game_state.h (GamePhase, PHASE_SETTINGS),
 *                 render/render.h (render_clear_piles, deal_anim, mirror_source_tid),
 *                 render/layout.h, render/anim.h, render/particle.h,
 *                 game/pass_phase.h, game/play_phase.h, game/turn_flow.h,
 *                 phase2/phase2_state.h, phase2/contract_logic.h, phase2/transmutation_logic.h,
 *                 phase2/phase2_defs.h, stdio.h
 * @deps-last-changed: 2026-04-04 — Uses mirror_source_tid for Mirror transmutation sprite morphing effect
 * ============================================================ */

#include "phase_transitions.h"

#include <stdio.h>

#include "core/card.h"
#include "render/render.h"
#include "render/anim.h"
#include "render/particle.h"
#include "phase2/contract_logic.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

void phase_transition_update(GameState *gs, RenderState *rs,
                             Phase2State *p2, PassPhaseState *pps,
                             PlayPhaseState *pls, TurnFlow *flow,
                             GamePhase *prev_phase,
                             bool *prev_hearts_broken)
{
    (void)p2;   /* used below for scoring effect indicators */
    (void)pps;  /* server handles pass setup */

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
        rs->scoring_screen_done = false;
        rs->scoring_ready_sent = false;
        rs->score_auto_timer = 0.0f;
        rs->score_auto_limit = 10.0f; /* overridden by update.c with bonus */

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

        /* Transmutation IDs that get reordered to the right of the row.
         * Order: Pendulum, Gatherer, Martyr (Martyr farthest right). */
        static const int reorder_ids[] = {6, 4, 3};
        static const int num_reorder = 3;

        /* Pass 0: determine visibility for all pile cards */
        for (int i = 0; i < rs->pile_card_count; i++) {
            CardVisual *pv = &rs->pile_cards[i];
            int pts = card_points(pv->card);
            /* Transmuted cards may override point value (e.g. Jokers = 0) */
            if (pv->transmute_id >= 0) {
                const TransmutationDef *td =
                    phase2_get_transmutation(pv->transmute_id);
                if (td && td->custom_points >= 0) pts = td->custom_points;
            }
            bool has_score_effect =
                transmute_effect_affects_score(pv->transmute_id);
            if (pts == 0 && !has_score_effect) {
                pv->opacity = 0.0f; /* hidden */
            } else {
                int owner = pv->pile_owner;
                if (owner < 0 || owner >= NUM_PLAYERS)
                    pv->opacity = 0.0f;
                else
                    pv->opacity = 1.0f; /* mark visible for passes below */
            }
        }

        /* Helper macro: assign scoring position to a visible pile card */
        #define ASSIGN_SCORING_POS(pv_ptr) do { \
            CardVisual *_pv = (pv_ptr); \
            int _owner = _pv->pile_owner; \
            int _ci = card_idx_per_player[_owner]++; \
            _pv->face_up = true; \
            _pv->start = _pv->position; \
            _pv->start_rotation = _pv->rotation; \
            Vector2 _target = layout_scoring_card_position( \
                _owner, _ci, cfg, &tbl); \
            float _tscale = 0.5f * cfg->scale; \
            _pv->scale = _tscale; \
            _pv->origin = (Vector2){ \
                CARD_WIDTH_REF * _tscale * 0.5f, \
                CARD_HEIGHT_REF * _tscale * 0.5f \
            }; \
            _pv->z_order = 200 + _owner * 20 + _ci; \
            float _delay = ((float)_owner * ANIM_SCORING_PLAYER_STAGGER + \
                            (float)_ci * ANIM_SCORING_CARD_STAGGER) * anim_mult; \
            anim_start(_pv, _target, 10.0f, \
                       ANIM_SCORING_FLY_DURATION, EASE_OUT_QUAD); \
            _pv->anim_delay = _delay; \
        } while (0)

        /* Pass 1: regular scoring cards (not in reorder list) */
        for (int i = 0; i < rs->pile_card_count; i++) {
            CardVisual *pv = &rs->pile_cards[i];
            if (pv->opacity <= 0.0f) continue;
            bool is_reorder = false;
            for (int r = 0; r < num_reorder; r++) {
                if (pv->transmute_id == reorder_ids[r]) {
                    is_reorder = true;
                    break;
                }
            }
            if (!is_reorder) {
                if (pv->scoring_hidden) continue;
                if (pv->transmute_id >= 0) {
                    const TransmutationDef *td = phase2_get_transmutation(pv->transmute_id);
                    if (td && td->hide_in_scoring) continue;
                }
                ASSIGN_SCORING_POS(pv);
            }
        }

        /* Passes 2-4: Pendulum (6), Gatherer (4), Martyr (3) */
        for (int r = 0; r < num_reorder; r++) {
            for (int i = 0; i < rs->pile_card_count; i++) {
                CardVisual *pv = &rs->pile_cards[i];
                if (pv->opacity <= 0.0f) continue;
                if (pv->transmute_id == reorder_ids[r]) {
                    if (pv->scoring_hidden) continue;
                    const TransmutationDef *td = phase2_get_transmutation(pv->transmute_id);
                    if (td && td->hide_in_scoring) continue;
                    ASSIGN_SCORING_POS(pv);
                }
            }
        }

        #undef ASSIGN_SCORING_POS

        /* Store per-player card counts and round-end effect indicators */
        for (int i = 0; i < NUM_PLAYERS; i++) {
            rs->scoring_cards_per_player[i] = card_idx_per_player[i];
            rs->scoring_martyr[i] = p2->enabled
                ? p2->round.transmute_round.martyr_flags[i] : 0;
            rs->scoring_gatherer[i] = p2->enabled
                ? p2->round.transmute_round.gatherer_reduction[i] : 0;
        }
    }

    /* Re-snapshot scores if server updates them during SCORING (Martyr/Gatherer) */
    if (gs->phase == PHASE_SCORING && *prev_phase == PHASE_SCORING &&
        rs->score_subphase <= SCORE_SUB_DISPLAY) {
        for (int i = 0; i < NUM_PLAYERS; i++) {
            if (gs->players[i].round_points != rs->displayed_round_points[i]) {
                rs->displayed_total_scores[i] =
                    gs->players[i].total_score - gs->players[i].round_points;
                rs->displayed_round_points[i] = gs->players[i].round_points;
                rs->score_countup_round[i] = gs->players[i].round_points;
            }
        }
    }

    /* Clear piles when entering dealing phase (new round) */
    if (gs->phase == PHASE_DEALING && *prev_phase != PHASE_DEALING) {
        render_clear_piles(rs);
        rs->trick_history_count = 0;
    }

    /* Deal animation complete — advance to passing phase */
    if (rs->deal_anim &&
        gs->phase == PHASE_DEALING && rs->deal_complete) {
        gs->phase = PHASE_PASSING;
        rs->sync_needed = true;
        rs->deal_complete = false;
        rs->deal_anim = false;
    }

    /* Reset flow when entering PHASE_PLAYING */
    if (gs->phase == PHASE_PLAYING && *prev_phase != PHASE_PLAYING) {
        flow_init(flow);
        render_clear_piles(rs);
        rs->trick_history_count = 0;
        rs->sync_needed = true;
            for (int ti = 0; ti < CARDS_PER_TRICK; ti++) {
            pls->current_tti.transmutation_ids[ti] = -1;
            pls->current_tti.transmuter_player[ti] = -1;
            pls->current_tti.resolved_effects[ti] = TEFFECT_NONE;
            pls->current_tti.fogged[ti] = false;
            pls->current_tti.fog_transmuter[ti] = -1;
        }
    }

    /* (Offline phase setup removed — server handles dealer, pass direction, draft state) */

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

    /* Don't track PHASE_SETTINGS — it's a temporary UI overlay, not a
     * real game transition.  Keeping prev_phase at the underlying game
     * phase prevents the SETTINGS→PLAYING return from triggering
     * flow_init / render_clear_piles / sync_needed. */
    if (gs->phase != PHASE_SETTINGS)
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
            if (cv->fog_mode == 2) continue; /* fogged — suppress particle leak */
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

    /* Particle burst: queen played (dark burst on any queen entering the trick) */
    static int s_prev_trick_count = 0;
    /* Reset mirror state when trick is collected */
    if (rs->trick_visual_count == 0 && s_prev_trick_count > 0) {
        for (int i = 0; i < CARDS_PER_TRICK; i++) {
            rs->mirror_source_tid[i] = -1;
            rs->mirror_morphed[i] = false;
            rs->mirror_morph_timer[i] = 0.0f;
        }
    }

    /* Mirror: tick per-slot delay timers, burst + sprite morph when ready */
    float dt = GetFrameTime();
    for (int s = 0; s < CARDS_PER_TRICK; s++) {
        if (rs->mirror_morph_timer[s] <= 0.0f) continue;
        rs->mirror_morph_timer[s] -= dt;
        if (rs->mirror_morph_timer[s] > 0.0f) continue;
        rs->mirror_morph_timer[s] = 0.0f;

        if (s >= rs->trick_visual_count) continue;
        int idx = rs->trick_visuals[s];
        if (idx < 0 || idx >= rs->card_count) continue;

        CardVisual *cv = &rs->cards[idx];
        Vector2 pos = cv->animating ? cv->target : cv->position;
        Vector2 center = {
            pos.x + rs->layout.card_width * 0.5f,
            pos.y + rs->layout.card_height * 0.5f
        };
        particle_spawn_burst_color(&rs->particles, center, 42,
                                   (Color){140, 50, 200, 255});
        cv->transmute_id = rs->mirror_source_tid[s];
        rs->mirror_morphed[s] = true;

        const TransmutationDef *src_td =
            phase2_get_transmutation(rs->mirror_source_tid[s]);
        if (src_td) {
            char mbuf[128];
            snprintf(mbuf, sizeof(mbuf),
                     "The Mirror becomes %s!", src_td->name);
            render_chat_log_push_rich(rs, mbuf, PURPLE,
                                      src_td->name,
                                      rs->mirror_source_tid[s]);
        }
    }

    if (rs->trick_visual_count > s_prev_trick_count) {
        int new_slot = rs->trick_visual_count - 1;
        int idx = rs->trick_visuals[new_slot];
        if (idx >= 0 && idx < rs->card_count) {
            Card c = rs->cards[idx].card;
            /* Queen of Spades (or Shadow Queen transmutation) */
            if (c.suit == SUIT_SPADES && c.rank == RANK_Q &&
                rs->cards[idx].fog_mode != 2) {
                CardVisual *cv = &rs->cards[idx];
                Vector2 pos = cv->animating ? cv->target : cv->position;
                Vector2 center = {
                    pos.x + rs->layout.card_width * 0.5f,
                    pos.y + rs->layout.card_height * 0.5f
                };
                particle_spawn_burst_color(&rs->particles, center, 36,
                                           (Color){20, 10, 30, 255});
            }

            /* Mirror: start delay timer for this slot */
            if (!rs->mirror_morphed[new_slot] &&
                rs->mirror_morph_timer[new_slot] <= 0.0f &&
                rs->mirror_source_tid[new_slot] >= 0 &&
                rs->cards[idx].fog_mode == 0) {
                rs->mirror_morph_timer[new_slot] = 0.6f;
            }
        }
    }
    s_prev_trick_count = rs->trick_visual_count;
}
