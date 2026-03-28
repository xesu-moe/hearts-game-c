/* ============================================================
 * @deps-implements: turn_flow.h
 * @deps-requires: turn_flow.h, core/game_state.h (game_state_current_player, game_state_complete_trick, game_state_complete_trick_with),
 *                 core/settings.h, core/trick.h, game/ai.h (ai_duel_choose, ai_duel_execute),
 *                 render/render.h (opponent_hover_active, render_chat_log_push),
 *                 render/layout.h (layout_board_center, layout_pile_position),
 *                 render/anim.h (ANIM_EFFECT_FLIGHT_DURATION, ANIM_DUEL_EXCHANGE_DURATION, CardVisual.inverted), game/play_phase.h,
 *                 phase2/transmutation_logic.h (transmute_trick_get_winner, transmute_trick_count_points, transmute_on_trick_complete, transmute_apply_round_end, TEFFECT_BOUNTY_REDIRECT_QOS, TEFFECT_INVERSION_NEGATE_POINTS),
 *                 phase2/contract_logic.h, phase2/phase2_defs.h,
 *                 phase2/phase2_state.h (shield_tricks_remaining[], binding_auto_win[])
 * @deps-last-changed: 2026-03-21 — Added Inversion effect: mark heart pile with inverted flag, negate QoS points
 * ============================================================ */

#include "turn_flow.h"

#include <stdio.h>

#include <stdlib.h>

#include "ai.h"
#include "core/input_cmd.h"
#include "core/trick.h"
#include "core/debug_log.h"
#include "render/render.h"
#include "render/layout.h"
#include "phase2/contract_logic.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

#ifdef DEBUG
static const char *flow_step_name(FlowStep s) {
    static const char *names[] = {
        "IDLE","WAIT_HUMAN","AI_THINK","CARD_ANIM","TRICK_DISP",
        "FOG_REVEAL","PILE_ANIM","COLLECTING","ROGUE_CHOOSE",
        "ROGUE_TO_CENTER","ROGUE_REVEAL","ROGUE_BACK",
        "DUEL_PICK_OPP","DUEL_TO_CENTER","DUEL_PICK_OWN",
        "DUEL_EXCHANGE","DUEL_RETURN","BETWEEN_TRICKS"
    };
    return (s >= 0 && s <= FLOW_BETWEEN_TRICKS) ? names[s] : "???";
}
#define FLOW_DBG(old, flow) \
    DBG(DBG_FLOW, "%s -> %s timer=%.3f", flow_step_name(old), flow_step_name((flow)->step), (flow)->timer)
#else
#define FLOW_DBG(old, flow) ((void)0)
#endif

/* Map player_id to screen position (matches render.c player_screen_pos) */
static const PlayerPosition pos_map[NUM_PLAYERS] = {
    POS_BOTTOM, POS_LEFT, POS_TOP, POS_RIGHT
};

/* Determine trick winner, copy trick visuals into pile, start pile animation.
 * Shared by FLOW_TRICK_DISPLAY and FLOW_FOG_REVEAL transitions. */
static void trick_to_pile_transition(TurnFlow *flow, GameState *gs,
                                     RenderState *rs, Phase2State *p2,
                                     PlayPhaseState *pps, float anim_mult)
{
    /* Use saved trick data (online) if available, else live GameState */
    const Trick *trick = flow->has_saved_trick
                             ? &flow->saved_trick : &gs->current_trick;
    const TrickTransmuteInfo *tti = flow->has_saved_trick
                                        ? &flow->saved_tti : &pps->current_tti;

    int winner;
    if (p2->enabled && trick_is_complete(trick)) {
        winner = transmute_trick_get_winner(trick, tti, p2);
    } else {
        winner = trick_get_winner(trick);
    }

    if (winner >= 0 && rs->trick_visual_count > 0) {
        const LayoutConfig *cfg = &rs->layout;

        /* Parasite: redirect pile to the player who played the Parasite card */
        int pile_player = winner;
        if (p2->enabled) {
            for (int i = 0; i < CARDS_PER_TRICK; i++) {
                if (tti->resolved_effects[i] ==
                    TEFFECT_PARASITE_REDIRECT_POINTS) {
                    int pp = trick->player_ids[i];
                    if (pp >= 0 && pp < NUM_PLAYERS) {
                        pile_player = pp;
                        break; /* First Parasite gets pile visuals */
                    }
                }
            }
        }

        /* Bounty: detect so Q♠ cards go to their player's pile */
        bool has_bounty = false;
        /* Inversion: detect so heart cards get inverted visual */
        bool has_inversion = false;
        if (p2->enabled) {
            for (int i = 0; i < CARDS_PER_TRICK; i++) {
                if (tti->resolved_effects[i] ==
                    TEFFECT_BOUNTY_REDIRECT_QOS)
                    has_bounty = true;
                if (tti->resolved_effects[i] ==
                    TEFFECT_INVERSION_NEGATE_POINTS)
                    has_inversion = true;
            }
        }

        for (int i = 0; i < rs->trick_visual_count; i++) {
            int src_idx = rs->trick_visuals[i];
            if (src_idx < 0 || src_idx >= rs->card_count) continue;
            if (rs->pile_card_count >= MAX_PILE_CARDS) break;

            /* Per-card pile owner: Bounty redirects Q♠ to its player */
            int card_pile_player = pile_player;
            if (has_bounty && i < trick->num_played &&
                trick->cards[i].suit == SUIT_SPADES &&
                trick->cards[i].rank == RANK_Q) {
                int qp = trick->player_ids[i];
                if (qp >= 0 && qp < NUM_PLAYERS)
                    card_pile_player = qp;
            }

            PlayerPosition card_spos = pos_map[card_pile_player];
            Vector2 card_pile_pos = layout_pile_position(card_spos, cfg);

            int pi = rs->pile_card_count;
            CardVisual *pv = &rs->pile_cards[pi];

            *pv = rs->cards[src_idx];
            pv->pile_owner = card_pile_player;
            if (p2->enabled)
                pv->transmute_id = tti->transmutation_ids[i];
            pv->face_up = false;
            pv->fog_mode = 0;
            pv->fog_reveal_t = 0.0f;
            pv->selected = false;
            pv->hovered = false;
            pv->hover_t = 0.0f;
            pv->use_bezier = false;
            pv->shielded = (p2->enabled && card_pile_player >= 0 &&
                            p2->shield_tricks_remaining[card_pile_player] > 0);
            /* Down arrow on hearts only (QoS also negated but no arrow by design) */
            pv->inverted = (has_inversion &&
                            i < trick->num_played &&
                            trick->cards[i].suit == SUIT_HEARTS);

            pv->start = pv->position;
            pv->start_rotation = pv->rotation;

            float scatter_x = ((float)(rand() % 7) - 3.0f) * cfg->scale;
            float scatter_y = ((float)(rand() % 7) - 3.0f) * cfg->scale;
            pv->target = (Vector2){
                card_pile_pos.x + scatter_x,
                card_pile_pos.y + scatter_y
            };
            float base_rot = (card_spos == POS_LEFT || card_spos == POS_RIGHT)
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
    flow->has_saved_trick = false;
}

void flow_update(TurnFlow *flow, GameState *gs, RenderState *rs,
                 Phase2State *p2, GameSettings *settings,
                 PlayPhaseState *pps, float dt, bool online)
{
#ifdef DEBUG
    FlowStep old_step = flow->step;
#endif

    if (gs->phase != PHASE_PLAYING) {
        rs->trick_anim_in_progress = false;
        rs->trick_visible_count = 0;
        flow->has_saved_trick = false;
        flow->step = FLOW_IDLE;
#ifdef DEBUG
        if (old_step != FLOW_IDLE) { FLOW_DBG(old_step, flow); }
#endif
        return;
    }

    flow->timer -= dt;

    switch (flow->step) {
    case FLOW_IDLE: {
        float anim_mult = settings_anim_multiplier(settings->anim_speed);
        /* Offline: complete trick → straight to TRICK_DISPLAY.
         * Online: fall through to card-by-card detection so each card
         * gets its own toss animation via the render sync block. */
        if (!online && trick_is_complete(&gs->current_trick)) {
            rs->trick_visible_count = CARDS_PER_TRICK;
            flow->step = FLOW_TRICK_DISPLAY;
            flow->timer = FLOW_TRICK_DISPLAY_TIME * anim_mult;
            rs->last_trick_winner = -1;

            return;
        }

        /* Snapshot hearts_broken at the start of each new trick */
        if (gs->current_trick.num_played == 0)
            flow->hearts_broken_at_trick_start = gs->hearts_broken;

        if (online) {
            /* Detect trick count regression: server advanced to a new trick
             * while we still had a stale prev_trick_count from the old one.
             * Also clear saved_trick so stale data doesn't block detection. */
            if (gs->current_trick.num_played < flow->prev_trick_count) {
                DBG(DBG_DETECT, "trick regression: num_played=%d < prev=%d, resetting",
                    gs->current_trick.num_played, flow->prev_trick_count);
                flow->prev_trick_count = 0;
                flow->has_saved_trick = false;
            }

            /* Detect new cards from server state updates and animate them */
            if (gs->current_trick.num_played > flow->prev_trick_count) {
                int play_idx = flow->prev_trick_count;
                int who = gs->current_trick.player_ids[play_idx];
                DBG(DBG_DETECT, "card IDLE: who=%d slot=%d prev=%d num_played=%d",
                    who, play_idx, flow->prev_trick_count, gs->current_trick.num_played);
                /* Save trick data so it survives server state overwrites */
                flow->saved_trick = gs->current_trick;
                flow->saved_tti = pps->current_tti;
                flow->has_saved_trick = true;
                rs->anim_play_player = (who >= 0) ? who : -1;
                rs->anim_trick_slot = play_idx;
                rs->trick_visible_count = play_idx + 1;
                rs->sync_needed = true;
                flow->prev_trick_count = play_idx + 1;
                flow->step = FLOW_CARD_ANIMATING;
                flow->timer = FLOW_CARD_ANIM_TIME * anim_mult;

            } else {
                int current = game_state_current_player(gs);
                if (current == 0) {
                    flow->step = FLOW_WAITING_FOR_HUMAN;
                    flow->prev_trick_count = gs->current_trick.num_played;
                    flow->turn_timer = FLOW_TURN_TIME_LIMIT;
    
                }
            }
        } else {
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
            if (online) {
                /* Timeout: pick a valid card and send to server */
                Card card;
                if (ai_select_card(gs, p2, 0, &card)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_PLAY_CARD,
                        .source_player = 0,
                        .card = { .card = card },
                    });
                }
                flow->turn_timer = FLOW_TURN_TIME_LIMIT; /* prevent re-fire */
            } else {
                ai_play_card(gs, rs, p2, pps, 0);
            }
        }

        if (gs->current_trick.num_played > flow->prev_trick_count) {
            int play_idx = flow->prev_trick_count;
            DBG(DBG_DETECT, "card WAIT_HUMAN: slot=%d prev=%d num_played=%d",
                play_idx, flow->prev_trick_count, gs->current_trick.num_played);
            if (online) {
                /* Save trick data so it survives server state overwrites */
                flow->saved_trick = gs->current_trick;
                flow->saved_tti = pps->current_tti;
                flow->has_saved_trick = true;
            }
            rs->sync_needed = true;
            rs->anim_play_player = gs->current_trick.player_ids[play_idx];
            rs->anim_trick_slot = play_idx;
            rs->trick_visible_count = play_idx + 1; /* show only up to card being animated */
            flow->prev_trick_count = play_idx + 1;
            flow->step = FLOW_CARD_ANIMATING;
            flow->timer = FLOW_CARD_ANIM_TIME * anim_mult;

        }
        break;
    }

    case FLOW_AI_THINKING: {
        /* Online: should never enter this state (server handles AI).
         * Fall back to IDLE if we somehow got here. */
        if (online) {
            flow->step = FLOW_IDLE;
            break;
        }
        float anim_mult = settings_anim_multiplier(settings->anim_speed);
        flow->turn_timer -= dt;
        if (flow->timer <= 0.0f) {
            int current = game_state_current_player(gs);
            if (current > 0) {
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
            if (online) {
                int total = flow->has_saved_trick
                                ? flow->saved_trick.num_played
                                : gs->current_trick.num_played;
                DBG(DBG_DETECT, "card_anim done: prev=%d total=%d saved=%d",
                    flow->prev_trick_count, total, flow->has_saved_trick);
                if (flow->prev_trick_count < total) {
                    rs->trick_anim_in_progress = false;
                    flow->step = FLOW_IDLE;
    
                } else if (total >= CARDS_PER_TRICK) {
                    if (!flow->has_saved_trick) {
                        flow->saved_trick = gs->current_trick;
                        flow->saved_tti = pps->current_tti;
                        flow->has_saved_trick = true;
                    }
                    rs->trick_visible_count = CARDS_PER_TRICK;
                    flow->step = FLOW_TRICK_DISPLAY;
                    flow->timer = FLOW_TRICK_DISPLAY_TIME *
                                  settings_anim_multiplier(settings->anim_speed);
                    rs->last_trick_winner = -1;
    
                } else {
                    rs->trick_anim_in_progress = false;
                    flow->step = FLOW_IDLE;
    
                }
            } else {
                rs->trick_anim_in_progress = false;
                flow->step = FLOW_IDLE;

            }
        }
        break;

    case FLOW_TRICK_DISPLAY: {
        float anim_mult = settings_anim_multiplier(settings->anim_speed);
        if (flow->timer <= 0.0f) {
            /* Check if any trick cards have fog — reveal first */
            bool has_fog = false;
            for (int i = 0; i < rs->trick_visual_count; i++) {
                int idx = rs->trick_visuals[i];
                if (idx >= 0 && idx < rs->card_count &&
                    rs->cards[idx].fog_mode > 0) {
                    has_fog = true;
                    break;
                }
            }
            if (has_fog) {
                flow->step = FLOW_FOG_REVEAL;
                flow->timer = FLOW_FOG_REVEAL_TIME * anim_mult;

                break;
            }

            trick_to_pile_transition(flow, gs, rs, p2, pps, anim_mult);

        }
        break;
    }

    case FLOW_FOG_REVEAL: {
        float anim_mult = settings_anim_multiplier(settings->anim_speed);
        float total_time = FLOW_FOG_REVEAL_TIME * anim_mult;
        float dissolve_time = FLOW_FOG_DISSOLVE_TIME * anim_mult;
        float elapsed = total_time - flow->timer;

        /* Animate fog_reveal_t from 1.0 to 0.0 over dissolve_time */
        if (elapsed < dissolve_time) {
            float t = elapsed / dissolve_time;
            float reveal = 1.0f - (t > 1.0f ? 1.0f : t);
            for (int i = 0; i < rs->trick_visual_count; i++) {
                int idx = rs->trick_visuals[i];
                if (idx >= 0 && idx < rs->card_count &&
                    rs->cards[idx].fog_mode > 0) {
                    rs->cards[idx].fog_reveal_t = reveal;
                }
            }
        } else {
            /* After dissolve: ensure fog fully cleared */
            for (int i = 0; i < rs->trick_visual_count; i++) {
                int idx = rs->trick_visuals[i];
                if (idx >= 0 && idx < rs->card_count) {
                    rs->cards[idx].fog_mode = 0;
                    rs->cards[idx].fog_reveal_t = 0.0f;
                }
            }
        }

        if (flow->timer <= 0.0f) {
            /* Announce hidden transmutation effects after fog reveal */
            const TrickTransmuteInfo *tti = flow->has_saved_trick
                ? &flow->saved_tti : &pps->current_tti;
            for (int i = 0; i < CARDS_PER_TRICK; i++) {
                if (tti->fogged[i] &&
                    tti->transmutation_ids[i] >= 0) {
                    int tid = tti->transmutation_ids[i];
                    const TransmutationDef *tdef = phase2_get_transmutation(tid);
                    /* Skip pure fog (no hidden effect to reveal) */
                    if (tdef && tdef->effect != TEFFECT_FOG_HIDDEN) {
                        char tmsg[CHAT_MSG_LEN];
                        snprintf(tmsg, sizeof(tmsg), "[%s] Revealed!",
                                 tdef->name);
                        render_chat_log_push_color(rs, tmsg, VIOLET);
                    }
                }
            }
            trick_to_pile_transition(flow, gs, rs, p2, pps, anim_mult);

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
            if (online) {
                /* Online: server already resolved trick, updated scores.
                 * Just transition to next state. */
                rs->sync_needed = true;
                flow->step = FLOW_BETWEEN_TRICKS;
                flow->timer = FLOW_BETWEEN_TRICKS_TIME *
                              settings_anim_multiplier(settings->anim_speed);

                break;
            }
            if (p2->enabled && trick_is_complete(&gs->current_trick)) {
                Trick saved_trick = gs->current_trick;
                TrickTransmuteInfo saved_tti = pps->current_tti;
                int winner = transmute_trick_get_winner(&saved_trick, &saved_tti, p2);
                int points = transmute_trick_count_points(&saved_trick, &saved_tti);

                /* Bounty: redirect Q♠ points to the player who played Q♠.
                 * Remaining (heart) points stay for winner/Parasite. */
                bool has_bounty = false;
                for (int bi = 0; bi < CARDS_PER_TRICK; bi++) {
                    if (saved_tti.resolved_effects[bi] ==
                        TEFFECT_BOUNTY_REDIRECT_QOS) {
                        has_bounty = true;
                        break;
                    }
                }
                if (has_bounty) {
                    bool has_trap = false;
                    bool has_inversion = false;
                    for (int ti = 0; ti < CARDS_PER_TRICK; ti++) {
                        if (saved_tti.resolved_effects[ti] ==
                            TEFFECT_TRAP_DOUBLE_WITH_QOS)
                            has_trap = true;
                        if (saved_tti.resolved_effects[ti] ==
                            TEFFECT_INVERSION_NEGATE_POINTS)
                            has_inversion = true;
                    }
                    /* Handles multiple Q♠ (e.g. Shadow Queen) — each redirected independently */
                    for (int qi = 0; qi < CARDS_PER_TRICK; qi++) {
                        if (saved_trick.cards[qi].suit == SUIT_SPADES &&
                            saved_trick.cards[qi].rank == RANK_Q) {
                            int qos_pts;
                            if (saved_tti.transmutation_ids[qi] >= 0) {
                                const TransmutationDef *td =
                                    phase2_get_transmutation(
                                        saved_tti.transmutation_ids[qi]);
                                qos_pts = (td && td->custom_points >= 0)
                                              ? td->custom_points
                                              : card_points(saved_trick.cards[qi]);
                            } else {
                                qos_pts = card_points(saved_trick.cards[qi]);
                            }
                            if (has_trap) qos_pts *= 2;
                            if (has_inversion) qos_pts = -qos_pts;

                            int qp = saved_trick.player_ids[qi];
                            if (qp >= 0 && qp < NUM_PLAYERS) {
                                if (p2->shield_tricks_remaining[qp] > 0) {
                                    TraceLog(LOG_INFO,
                                             "TRANSMUTE: Shield absorbed Bounty "
                                             "QoS for player %d", qp);
                                    qos_pts = 0;
                                }
                                gs->players[qp].round_points += qos_pts;
                                points -= qos_pts;
                                TraceLog(LOG_INFO,
                                         "TRANSMUTE: Bounty redirected %d QoS "
                                         "points to player %d", qos_pts, qp);
                            }
                        }
                    }
                }

                /* Parasite: redirect points to card player(s).
                 * Points are added to round_points directly. Multiple
                 * Parasites each get full points (duplication by design).
                 * Shoot-the-moon: if a Parasite player accumulates 26,
                 * it triggers legitimately (they absorbed all points).
                 * Note: if Bounty is active, Q♠ points are already
                 * extracted — Parasite only gets remaining heart points. */
                bool has_parasite = false;
                for (int pi = 0; pi < CARDS_PER_TRICK; pi++) {
                    if (saved_tti.resolved_effects[pi] ==
                        TEFFECT_PARASITE_REDIRECT_POINTS) {
                        has_parasite = true;
                        int pp = saved_trick.player_ids[pi];
                        int pp_points = points;
                        if (pp >= 0 && p2->shield_tricks_remaining[pp] > 0) {
                            pp_points = 0;
                            TraceLog(LOG_INFO,
                                     "TRANSMUTE: Shield absorbed Parasite "
                                     "redirect for player %d", pp);
                        }
                        gs->players[pp].round_points += pp_points;
                        TraceLog(LOG_INFO,
                                 "TRANSMUTE: Parasite redirected %d points "
                                 "to player %d", pp_points, pp);
                    }
                }

                if (has_parasite) {
                    /* Shield decrement still happens for all players */
                    for (int si = 0; si < NUM_PLAYERS; si++) {
                        if (p2->shield_tricks_remaining[si] > 0)
                            p2->shield_tricks_remaining[si]--;
                    }
                    /* Winner gets 0 points but still leads */
                    if (!game_state_complete_trick_with(gs, winner, 0)) break;
                } else {
                    /* Shield: zero points if winner has active shield */
                    if (winner >= 0 &&
                        p2->shield_tricks_remaining[winner] > 0) {
                        points = 0;
                        TraceLog(LOG_INFO,
                                 "TRANSMUTE: Shield absorbed trick for "
                                 "player %d", winner);
                    }
                    /* Decrement all active shield counters each trick */
                    for (int si = 0; si < NUM_PLAYERS; si++) {
                        if (p2->shield_tricks_remaining[si] > 0)
                            p2->shield_tricks_remaining[si]--;
                    }
                    if (!game_state_complete_trick_with(gs, winner, points))
                        break;
                }
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
                    /* Binding: consume ALL active binding flags (not just
                     * winner's). A bound player who lost still used their
                     * "next trick". Chaining works because
                     * transmute_on_trick_complete already re-set the flag. */
                    for (int bi = 0; bi < NUM_PLAYERS; bi++) {
                        if (p2->binding_auto_win[bi]) {
                            p2->binding_auto_win[bi] = 0;
                        }
                    }
                }
                /* Reset trick transmute info for next trick */
                for (int ti = 0; ti < CARDS_PER_TRICK; ti++) {
                    pps->current_tti.transmutation_ids[ti] = -1;
                    pps->current_tti.transmuter_player[ti] = -1;
                    pps->current_tti.resolved_effects[ti] = TEFFECT_NONE;
                    pps->current_tti.fogged[ti] = false;
                    pps->current_tti.fog_transmuter[ti] = -1;
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
        } else if (!online && flow->timer <= 0.0f) {
            /* Offline: auto-choose on timeout. Online: server handles. */
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
        } else if (!online && flow->timer <= 0.0f) {
            /* Offline: timeout skip. Online: server handles. */
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
        } else if (!online && flow->timer <= 0.0f) {
            /* Offline: timeout return card. Online: server handles. */
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
            rs->trick_anim_in_progress = false;
            flow->has_saved_trick = false;
            /* Set to current num_played (not 0) to avoid re-detecting
             * stale trick cards left over from deferred state updates.
             * The regression detector (above) handles the reset when
             * fresh state arrives with a lower num_played. */
            flow->prev_trick_count = gs->current_trick.num_played;
            rs->trick_visible_count = 0; /* reset cap — sync shows current state */
            rs->sync_needed = true; /* Re-sync now that trick data is unprotected */
            flow->step = FLOW_IDLE;

        }
        break;
    }

#ifdef DEBUG
    if (flow->step != old_step) {
        DBG(DBG_FLOW, "%s -> %s timer=%.3f prev_trick=%d",
            flow_step_name(old_step), flow_step_name(flow->step),
            flow->timer, flow->prev_trick_count);
    }
#endif
}
