/* ============================================================
 * @deps-implements: turn_flow.h
 * @deps-requires: turn_flow.h, core/game_state.h (GameState functions),
 *                 core/settings.h, core/trick.h, render/render.h,
 *                 render/layout.h, render/anim.h, game/play_phase.h,
 *                 phase2/transmutation_logic.h,
 *                 phase2/phase2_defs.h, phase2/phase2_state.h
 * @deps-last-changed: 2026-04-13 — turn timer now server-authoritative; removed local auto-play and select_valid_card helper
 * ============================================================ */

#include "turn_flow.h"

#include <stdio.h>

#include <stdlib.h>
#include "core/trick.h"
#include "render/render.h"
#include "render/layout.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

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
    if (flow->has_saved_trick && pps->server_trick_winner >= 0) {
        /* Online mode: use server-authoritative winner (Roulette determinism) */
        winner = pps->server_trick_winner;
    } else if (p2->enabled && trick_is_complete(trick)) {
        winner = transmute_trick_get_winner(trick, tti, p2);
    } else {
        winner = trick_get_winner(trick);
    }

    /* Save trick to history for chat tooltip */
    if (winner >= 0 && rs->trick_history_count < MAX_TRICKS_PER_ROUND) {
        TrickRecord *rec = &rs->trick_history[rs->trick_history_count++];
        for (int i = 0; i < CARDS_PER_TRICK; i++) {
            rec->cards[i] = trick->cards[i];
            rec->player_ids[i] = trick->player_ids[i];
            rec->transmute_ids[i] = tti->transmutation_ids[i];
        }
        rec->winner = winner;
        rec->num_played = trick->num_played;
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
        /* Trap: detect QoS presence so untriggered traps are hidden in scoring */
        bool has_qos = false;
        if (p2->enabled) {
            for (int i = 0; i < CARDS_PER_TRICK; i++) {
                if (tti->resolved_effects[i] ==
                    TEFFECT_BOUNTY_REDIRECT_QOS)
                    has_bounty = true;
                if (tti->resolved_effects[i] ==
                    TEFFECT_INVERSION_NEGATE_POINTS)
                    has_inversion = true;
                if (i < trick->num_played &&
                    trick->cards[i].suit == SUIT_SPADES &&
                    trick->cards[i].rank == RANK_Q)
                    has_qos = true;
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
            if (p2->enabled) {
                int tid = tti->transmutation_ids[i];
                if (tid >= 0) {
                    const TransmutationDef *td = phase2_get_transmutation(tid);
                    if (td && td->effect == TEFFECT_MIRROR &&
                        rs->mirror_source_tid[i] >= 0) {
                        tid = rs->mirror_source_tid[i];
                    }
                }
                pv->transmute_id = tid;
            }
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
            /* Hide untriggered Trap cards from scoring screen */
            pv->scoring_hidden = false;
            if (p2->enabled && pv->transmute_id >= 0) {
                const TransmutationDef *ptd = phase2_get_transmutation(pv->transmute_id);
                if (ptd && ptd->effect == TEFFECT_TRAP_DOUBLE_WITH_QOS && !has_qos)
                    pv->scoring_hidden = true;
            }

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

/* Launch multiple Rogue card flights from hand to board center.
 * Arranges cards in a horizontal line. Sets face_up, z_order, starts animations. */
static void rogue_launch_flights(TurnFlow *flow, RenderState *rs,
                                 const Phase2State *p2,
                                 int rp, int count, const char *msg,
                                 float anim_m)
{

    if (count <= 0) return;

    Vector2 center = layout_board_center(&rs->layout);
    center.y -= rs->layout.card_height * 0.8f; /* shift up toward true center */
    float card_w = rs->layout.card_width * 1.1f; /* spacing between cards */
    float total_w = card_w * (float)(count - 1);
    float start_x = center.x - total_w * 0.5f;

    flow->rogue_staged_cv_count = 0;
    int hand_vis = rs->hand_visual_counts[rp];

    /* We pick the first N card visuals from the opponent's hand.
     * (Opponent cards are all face-down/identical, so any N will do.) */
    for (int i = 0; i < count && i < hand_vis; i++) {
        int cv_idx = rs->hand_visuals[rp][i];
        if (cv_idx < 0 || cv_idx >= rs->card_count) continue;

        rs->cards[cv_idx].card = p2->round.transmute_round.rogue_revealed_cards[i];
        rs->cards[cv_idx].face_up = true;
        rs->cards[cv_idx].z_order = 200 + i;

        Vector2 target = { start_x + card_w * (float)i, center.y };
        anim_start_scaled(&rs->cards[cv_idx], target, 0.0f,
                          1.4f, ANIM_EFFECT_FLIGHT_DURATION * anim_m,
                          EASE_IN_OUT_QUAD);

        flow->rogue_staged_cv_indices[flow->rogue_staged_cv_count] = cv_idx;
        flow->rogue_staged_cv_count++;
    }

    rs->staged_rogue_cv_count = flow->rogue_staged_cv_count;
    for (int i = 0; i < flow->rogue_staged_cv_count; i++)
        rs->staged_rogue_cv_indices[i] = flow->rogue_staged_cv_indices[i];

    rs->rogue_border_active = false; /* activated when cards arrive */
    rs->rogue_border_progress = 0.0f;

    render_chat_log_push(rs, msg);
    rs->opponent_hover_active = false;
    rs->suit_hover_active = false;
    flow->step = FLOW_ROGUE_ANIM_TO_CENTER;
    flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
}

/* Try to start the Rogue effect. Returns true if flow was redirected. */
static bool try_start_rogue(TurnFlow *flow, GameState *gs, RenderState *rs,
                            Phase2State *p2, GameSettings *settings)
{
    (void)settings;
    if (!p2->enabled || gs->phase != PHASE_PLAYING ||
        p2->round.transmute_round.rogue_pending_winner < 0)
        return false;

    /* Prevent deferred state updates from re-triggering a rogue
     * that was already handled on the client side. */
    if (flow->rogue_effect_handled) {
        p2->round.transmute_round.rogue_pending_winner = -1;
        return false;
    }

    int rw = p2->round.transmute_round.rogue_pending_winner;
    p2->round.transmute_round.rogue_pending_winner = -1;
    flow->rogue_effect_handled = true;
    flow->rogue_winner = rw;
    flow->rogue_target_player = -1;
    flow->rogue_reveal_player = -1;
    flow->rogue_revealed_count = 0;
    flow->rogue_staged_cv_count = 0;

    if (rw == 0) {
        /* Human winner: wait for click with hover */
        flow->step = FLOW_ROGUE_CHOOSING;
        flow->timer = FLOW_ROGUE_CHOOSE_TIME;
        rs->opponent_hover_active = true;
        rs->opponent_hover_player = -1;
        rs->opponent_border_t = 0.0f;
        render_chat_log_push(rs, "Rogue: Choose an opponent to reveal cards from!");
        rs->sync_needed = true;
    } else {
        /* Non-winner (other human or AI): passively watch the reveal.
         * Server will broadcast rogue_revealed_count when the picker
         * commits a suit; FLOW_ROGUE_WAITING consumes it. */
        flow->step = FLOW_ROGUE_WAITING;
    }
    return true;
}

/* Try to start the Duel effect. Returns true if flow was redirected. */
static bool try_start_duel(TurnFlow *flow, GameState *gs, RenderState *rs,
                           Phase2State *p2, GameSettings *settings)
{
    (void)settings;
    if (!p2->enabled || gs->phase != PHASE_PLAYING ||
        p2->round.transmute_round.duel_pending_winner < 0)
        return false;

    /* Prevent deferred state updates from re-triggering a duel
     * that was already handled on the client side. */
    if (flow->duel_effect_handled) {
        p2->round.transmute_round.duel_pending_winner = -1;
        return false;
    }

    int dw = p2->round.transmute_round.duel_pending_winner;
    p2->round.transmute_round.duel_pending_winner = -1;
    flow->duel_effect_handled = true;
    flow->duel_winner = dw;
    flow->duel_target_player = -1;
    flow->duel_target_card_idx = -1;
    flow->duel_own_card_idx = -1;
    flow->duel_returned = false;
    flow->duel_staged_cv_idx = -1;
    flow->duel_own_cv_idx = -1;
    flow->duel_ai_decided = false;
    flow->duel_watching = false;

    if (dw == 0) {
        /* Human winner: wait for click with hover */
        flow->step = FLOW_DUEL_PICK_OPPONENT;
        flow->timer = FLOW_DUEL_CHOOSE_TIME;
        rs->opponent_hover_active = true;
        rs->opponent_hover_player = -1;
        rs->opponent_border_t = 0.0f;
        rs->sync_needed = true;
    } else {
        /* Non-winner: watch the duel animation passively.
         * Skip PICK_OPPONENT — go straight to WAITING for server state. */
        flow->duel_watching = true;
        flow->step = FLOW_DUEL_WAITING;
    }
    return true;
}

void flow_init(TurnFlow *flow)
{
    flow->step = FLOW_IDLE;
    flow->timer = 0.0f;
    flow->turn_time_limit = FLOW_TURN_TIME_LIMIT;
    flow->turn_timer = flow->turn_time_limit;
    flow->animating_player = -1;
    flow->prev_trick_count = 0;
    flow->hearts_broken_at_trick_start = false;
    flow->rogue_winner = -1;
    flow->rogue_target_player = -1;
    flow->rogue_reveal_player = -1;
    flow->rogue_revealed_count = 0;
    flow->rogue_staged_cv_count = 0;
    flow->duel_winner = -1;
    flow->duel_target_player = -1;
    flow->duel_target_card_idx = -1;
    flow->duel_own_card_idx = -1;
    flow->duel_returned = false;
    flow->duel_staged_cv_idx = -1;
    flow->duel_own_cv_idx = -1;
    flow->duel_ai_decided = false;
    flow->duel_watching = false;
    flow->duel_just_ended = false;
    flow->has_saved_trick = false;
}

void flow_update(TurnFlow *flow, GameState *gs, RenderState *rs,
                 Phase2State *p2, GameSettings *settings,
                 PlayPhaseState *pps, float dt)
{
    if (gs->phase != PHASE_PLAYING) {
        /* Settings screen is a temporary overlay — preserve flow state
         * so we resume cleanly on return. The turn clock is authoritative
         * on the server and continues to tick regardless of local UI. */
        if (gs->phase == PHASE_SETTINGS) {
            return;
        }
        rs->trick_anim_in_progress = false;
        rs->trick_visible_count = 0;
        flow->has_saved_trick = false;
        flow->step = FLOW_IDLE;
        return;
    }

    flow->timer -= dt;

    switch (flow->step) {
    case FLOW_IDLE: {
        float anim_mult = settings_anim_multiplier(settings->anim_speed);

        /* Check for pending rogue/duel effects from network state updates.
         * In online play, the state carrying these flags is deferred during
         * trick animations and only consumed once flow returns to IDLE,
         * AFTER the COLLECTING check has already passed. */
        if (try_start_rogue(flow, gs, rs, p2, settings)) break;
        if (try_start_duel(flow, gs, rs, p2, settings)) break;

        /* Snapshot hearts_broken at the start of each new trick */
        if (gs->current_trick.num_played == 0)
            flow->hearts_broken_at_trick_start = gs->hearts_broken;

        /* Detect trick count regression: server advanced to a new trick
         * while we still had a stale prev_trick_count from the old one.
         * Also clear saved_trick so stale data doesn't block detection. */
        if (gs->current_trick.num_played < flow->prev_trick_count) {
            flow->prev_trick_count = 0;
            flow->has_saved_trick = false;
        }

        /* Detect new cards from server state updates and animate them */
        if (gs->current_trick.num_played > flow->prev_trick_count) {
            int play_idx = flow->prev_trick_count;
            int who = gs->current_trick.player_ids[play_idx];
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
            pps->card_played_sfx = true;

        } else {
            int current = game_state_current_player(gs);
            if (current == 0) {
                flow->step = FLOW_WAITING_FOR_HUMAN;
                flow->prev_trick_count = gs->current_trick.num_played;
            }
        }
        break;
    }

    case FLOW_WAITING_FOR_HUMAN: {
        float anim_mult = settings_anim_multiplier(settings->anim_speed);
        /* Turn timeout is now enforced server-side (server_game.c
         * SV_PLAY_WAIT_TURN). The server auto-plays a valid card and
         * broadcasts the resulting state, so the client doesn't need a
         * local fallback timer here. */

        if (gs->current_trick.num_played > flow->prev_trick_count) {
            int play_idx = flow->prev_trick_count;
            /* Save trick data so it survives server state overwrites */
            flow->saved_trick = gs->current_trick;
            flow->saved_tti = pps->current_tti;
            flow->has_saved_trick = true;
            rs->sync_needed = true;
            rs->anim_play_player = gs->current_trick.player_ids[play_idx];
            rs->anim_trick_slot = play_idx;
            rs->trick_visible_count = play_idx + 1; /* show only up to card being animated */
            flow->prev_trick_count = play_idx + 1;
            flow->step = FLOW_CARD_ANIMATING;
            flow->timer = FLOW_CARD_ANIM_TIME * anim_mult;
            pps->card_played_sfx = true;

        }
        break;
    }

    case FLOW_AI_THINKING:
        flow->step = FLOW_IDLE;
        break;

    case FLOW_CARD_ANIMATING:
        if (flow->timer <= 0.0f) {
            int total = flow->has_saved_trick
                            ? flow->saved_trick.num_played
                            : gs->current_trick.num_played;
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
                /* NOTE: Do NOT clear server_trick_winner here.
                 * State updates are deferred during trick animations
                 * (would_defer in main.c), so the value set before
                 * entering FLOW_CARD_ANIMATING is the correct one
                 * for this trick and won't be re-sent. */

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
                        snprintf(tmsg, sizeof(tmsg), "%s revealed!",
                                 tdef->name);
                        render_chat_log_push_rich(rs, tmsg, PURPLE,
                                                  tdef->name, tid);
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
            /* Server already resolved trick, updated scores.
             * Check for post-trick effects before moving on. */
            rs->sync_needed = true;
            if (try_start_rogue(flow, gs, rs, p2, settings)) break;
            if (try_start_duel(flow, gs, rs, p2, settings)) break;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = FLOW_BETWEEN_TRICKS_TIME *
                          settings_anim_multiplier(settings->anim_speed);
        }
        break;

    case FLOW_ROGUE_CHOOSING:
        /* Opponent selection handled by process_input.
         * INPUT_CMD_ROGUE_PICK routes through main.c to set
         * rogue_target_player and transition to FLOW_ROGUE_SUIT_CHOOSING. */
        break;

    case FLOW_ROGUE_SUIT_CHOOSING:
        /* Suit selection handled by process_input.
         * INPUT_CMD_ROGUE_REVEAL routes through main.c to server.
         * We stay here until server responds in FLOW_ROGUE_WAITING. */
        break;

    case FLOW_ROGUE_WAITING: {
        /* Wait for server to broadcast the revealed cards */
        int rcount = p2->round.transmute_round.rogue_revealed_count;
        if (rcount >= 0) {
            float anim_m = settings_anim_multiplier(settings->anim_speed);
            int rp = flow->rogue_reveal_player;
            if (rp < 0)
                rp = p2->round.transmute_round.rogue_chosen_target;
            flow->rogue_revealed_count = rcount;
            p2->round.transmute_round.rogue_revealed_count = -1;

            if (rcount == 0) {
                /* No cards of that suit */
                char msg[CHAT_MSG_LEN];
                snprintf(msg, sizeof(msg), "%s hasn't got any %s cards",
                         p2_player_name(rp),
                         (const char *[]){"Clubs","Diamonds","Spades","Hearts"}
                            [p2->round.transmute_round.rogue_chosen_suit]);
                render_chat_log_push(rs, msg);
                rs->opponent_hover_active = false;
                rs->suit_hover_active = false;
                flow->step = FLOW_ROGUE_REVEAL;
                flow->timer = FLOW_ROGUE_NO_CARDS_TIME;
            } else {
                char msg[CHAT_MSG_LEN];
                snprintf(msg, sizeof(msg), "Rogue: Revealing %d of %s's cards!",
                         rcount, p2_player_name(rp));
                rogue_launch_flights(flow, rs, p2, rp, rcount, msg, anim_m);
            }
        }
        break;
    }

    case FLOW_ROGUE_ANIM_TO_CENTER:
        if (flow->timer <= 0.0f) {
            rs->rogue_border_active = true;
            rs->rogue_border_progress = 0.0f;
            flow->step = FLOW_ROGUE_REVEAL;
            flow->timer = FLOW_ROGUE_REVEAL_TIME;
        }
        break;

    case FLOW_ROGUE_REVEAL: {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        /* Advance border progress: 0→1 over FLOW_ROGUE_REVEAL_TIME */
        if (rs->rogue_border_active) {
            rs->rogue_border_progress =
                1.0f - (flow->timer / FLOW_ROGUE_REVEAL_TIME);
            if (rs->rogue_border_progress > 1.0f)
                rs->rogue_border_progress = 1.0f;
        }
        if (flow->timer <= 0.0f) {
            rs->rogue_border_active = false;
            /* Animate all cards back to hand */
            int rp = flow->rogue_reveal_player;
            if (rp >= 0 && flow->rogue_staged_cv_count > 0) {
                Vector2 positions[MAX_HAND_SIZE];
                float rotations[MAX_HAND_SIZE];
                int lcount = 0;
                layout_hand_positions(pos_map[rp],
                                      rs->hand_visual_counts[rp],
                                      &rs->layout, positions, rotations,
                                      &lcount);
                for (int i = 0; i < flow->rogue_staged_cv_count; i++) {
                    int cv_idx = flow->rogue_staged_cv_indices[i];
                    if (cv_idx < 0 || cv_idx >= rs->card_count) continue;
                    rs->cards[cv_idx].face_up = false;
                    Vector2 ret_pos = (i < lcount) ? positions[i]
                                                    : rs->cards[cv_idx].position;
                    float ret_rot = (i < lcount) ? rotations[i] : 0.0f;
                    anim_start_scaled(&rs->cards[cv_idx], ret_pos, ret_rot,
                                      1.0f, ANIM_EFFECT_FLIGHT_DURATION * anim_m,
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
            /* Reset z_order and hover state for all staged cards */
            for (int i = 0; i < flow->rogue_staged_cv_count; i++) {
                int cv_idx = flow->rogue_staged_cv_indices[i];
                if (cv_idx >= 0 && cv_idx < rs->card_count) {
                    rs->cards[cv_idx].z_order = 0;
                    rs->cards[cv_idx].hover_t = 0.0f;
                    rs->cards[cv_idx].hovered = false;
                }
            }
            flow->rogue_winner = -1;
            flow->rogue_target_player = -1;
            flow->rogue_reveal_player = -1;
            flow->rogue_revealed_count = 0;
            flow->rogue_staged_cv_count = 0;
            rs->staged_rogue_cv_count = 0;
            /* Check for pending Duel effect before going to BETWEEN_TRICKS */
            if (try_start_duel(flow, gs, rs, p2, settings)) break;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = FLOW_BETWEEN_TRICKS_TIME * anim_m;
        }
        break;
    }

    case FLOW_DUEL_PICK_OPPONENT: {
        /* Opponent selection handled by process_input + inline routing.
         * Transitions to FLOW_DUEL_WAITING when player picks opponent. */
        if (flow->timer <= 0.0f) {
            /* Timed out — server auto-picks opponent. Transition to WAITING
             * so the client can receive the server's chosen target/card. */
            rs->opponent_hover_active = false;
            rs->opponent_hover_player = -1;
            flow->step = FLOW_DUEL_WAITING;
            /* No timer needed — WAITING polls for server state */
        }
        break;
    }

    case FLOW_DUEL_WAITING: {
        int chosen = p2->round.transmute_round.duel_chosen_card_idx;
        int target = flow->duel_target_player;
        /* If target is unset (-1), use the server's auto-picked target.
         * This happens on PICK_OPPONENT timeout and for watchers. */
        if (target < 0 && p2->round.transmute_round.duel_chosen_target >= 0) {
            target = p2->round.transmute_round.duel_chosen_target;
            flow->duel_target_player = target;
        }
        if (chosen >= 0 && target >= 0) {
            float anim_m = settings_anim_multiplier(settings->anim_speed);
            flow->duel_target_card_idx = chosen;
            p2->round.transmute_round.duel_chosen_card_idx = -1;
            p2->round.transmute_round.duel_chosen_target = -1;
            /* Animate opponent's card to center (same scale/position as rogue) */
            if (chosen < rs->hand_visual_counts[target]) {
                int cv_idx = rs->hand_visuals[target][chosen];
                if (cv_idx >= 0 && cv_idx < rs->card_count) {
                    Card revealed = p2->round.transmute_round.duel_revealed_card;
                    /* Only winner (seat 0) or victim (target==0) see the face.
                     * Spectators always get card back, regardless of revealed data. */
                    bool can_see = (flow->duel_winner == 0 || target == 0) &&
                                   (revealed.suit >= 0 && revealed.rank >= 0);
                    if (can_see)
                        rs->cards[cv_idx].card = revealed;
                    rs->cards[cv_idx].face_up = can_see;
                    rs->cards[cv_idx].z_order = 200;
                    Vector2 center = layout_board_center(&rs->layout);
                    center.y -= rs->layout.card_height * 0.3f;
                    anim_start_scaled(&rs->cards[cv_idx], center, 0.0f,
                                      1.4f, ANIM_EFFECT_FLIGHT_DURATION * anim_m,
                                      EASE_IN_OUT_QUAD);
                    flow->duel_staged_cv_idx = cv_idx;
                    rs->staged_duel_cv_idx = cv_idx;
                }
            }
            flow->step = FLOW_DUEL_ANIM_TO_CENTER;
            flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
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
            } else if (flow->duel_watching) {
                /* Non-winner: wait passively for server to resolve */
                flow->step = FLOW_DUEL_PICK_OWN;
                flow->timer = 999.0f; /* won't time out — server resolves */
            } else {
                /* Human winner: pick own card */
                flow->step = FLOW_DUEL_PICK_OWN;
                flow->timer = FLOW_DUEL_CHOOSE_TIME;
                rs->duel_border_active = true;
                rs->duel_border_progress = 0.0f;
            }
        }
        break;

    case FLOW_DUEL_PICK_OWN: {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        /* Non-winner watching: wait for server to resolve */
        if (flow->duel_watching) {
            if (p2->round.transmute_round.duel_pending_winner < 0) {
                bool was_swap = (p2->round.transmute_round.duel_was_swap > 0);
                if (was_swap && flow->duel_staged_cv_idx >= 0 &&
                    flow->duel_staged_cv_idx < rs->card_count) {
                    /* Swap: animate staged card → winner's hand */
                    int dw = flow->duel_winner;
                    int dp = flow->duel_target_player;
                    {
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
                    /* Also animate a card from winner's hand → victim */
                    {
                        int mid = rs->hand_visual_counts[dw] / 2;
                        if (mid >= 0 && mid < rs->hand_visual_counts[dw]) {
                            int own_cv = rs->hand_visuals[dw][mid];
                            if (own_cv >= 0 && own_cv < rs->card_count &&
                                own_cv != flow->duel_staged_cv_idx) {
                                rs->cards[own_cv].z_order = 200;
                                rs->cards[own_cv].face_up = false;
                                Vector2 dest;
                                float rot = 0.0f;
                                if (dp == 0) {
                                    /* Victim: fly to pass preview staging area */
                                    Vector2 preview[1];
                                    layout_pass_preview_positions(1, &rs->layout, preview);
                                    dest = preview[0];
                                } else {
                                    /* Spectator: fly to victim's hand center */
                                    Vector2 positions[MAX_HAND_SIZE];
                                    float rotations[MAX_HAND_SIZE];
                                    int count = 0;
                                    layout_hand_positions(pos_map[dp],
                                                          rs->hand_visual_counts[dp],
                                                          &rs->layout, positions, rotations,
                                                          &count);
                                    dest = (count > 0) ? positions[count / 2]
                                                       : layout_board_center(&rs->layout);
                                    rot = (count > 0) ? rotations[count / 2] : 0.0f;
                                }
                                anim_start(&rs->cards[own_cv], dest, rot,
                                           ANIM_DUEL_EXCHANGE_DURATION * anim_m,
                                           EASE_IN_OUT_QUAD);
                                flow->duel_own_cv_idx = own_cv;
                                rs->staged_duel_own_cv_idx = own_cv;
                            }
                        }
                    }
                    flow->step = FLOW_DUEL_ANIM_EXCHANGE;
                    flow->timer = ANIM_DUEL_EXCHANGE_DURATION * anim_m;
                } else {
                    /* Return: animate staged card → back to opponent hand */
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
                    flow->step = FLOW_DUEL_ANIM_RETURN;
                    flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
                }
                p2->round.transmute_round.duel_was_swap = -1;
            }
            break;
        }
        /* Drive border progress: 0→1 over FLOW_DUEL_CHOOSE_TIME */
        if (rs->duel_border_active) {
            rs->duel_border_progress =
                1.0f - (flow->timer / FLOW_DUEL_CHOOSE_TIME);
            if (rs->duel_border_progress > 1.0f)
                rs->duel_border_progress = 1.0f;
            if (rs->duel_border_progress < 0.0f)
                rs->duel_border_progress = 0.0f;
        }
        if (flow->timer <= 0.0f && !flow->duel_returned && flow->duel_own_card_idx < 0) {
            /* Timed out — animate card back to opponent's hand (same as manual return) */
            rs->duel_border_active = false;
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
            flow->step = FLOW_DUEL_ANIM_RETURN;
            flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
            break;
        }
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
            rs->duel_border_active = false;
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
            rs->duel_border_active = false;
            flow->step = FLOW_DUEL_ANIM_EXCHANGE;
            flow->timer = ANIM_DUEL_EXCHANGE_DURATION * anim_m;
        }
        break;
    }

    case FLOW_DUEL_ANIM_EXCHANGE: {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        if (flow->timer <= 0.0f) {
            /* Clear face_up on staged card */
            if (flow->duel_staged_cv_idx >= 0 &&
                flow->duel_staged_cv_idx < rs->card_count)
                rs->cards[flow->duel_staged_cv_idx].face_up = false;

            /* Victim receive: staged card done, now reveal the incoming card */
            if (flow->duel_watching && flow->duel_target_player == 0 &&
                flow->duel_own_cv_idx >= 0 && flow->duel_own_cv_idx < rs->card_count &&
                flow->duel_target_card_idx >= 0 &&
                flow->duel_target_card_idx < gs->players[0].hand.count) {
                /* Clean up staged card (stolen card reached winner) */
                if (flow->duel_staged_cv_idx >= 0 &&
                    flow->duel_staged_cv_idx < rs->card_count)
                    rs->cards[flow->duel_staged_cv_idx].z_order = 0;
                flow->duel_staged_cv_idx = -1;
                rs->staged_duel_cv_idx = -1;

                /* Reveal the new card at staging position */
                CardVisual *cv = &rs->cards[flow->duel_own_cv_idx];
                Card new_card = gs->players[0].hand.cards[flow->duel_target_card_idx];
                cv->card = new_card;
                cv->face_up = true;
                /* Scale-reveal in place (like pass_start_reveal) */
                Vector2 preview[1];
                layout_pass_preview_positions(1, &rs->layout, preview);
                float human_scale = rs->layout.scale;
                float cw_s = rs->layout.card_width;
                float ch_s = rs->layout.card_height;
                cv->origin = (Vector2){cw_s * 0.5f, ch_s};
                anim_start_scaled(cv, preview[0], 0.0f,
                                  human_scale, ANIM_PASS_REVEAL_FLY_DURATION,
                                  EASE_OUT_BACK);
                flow->step = FLOW_DUEL_RECEIVE_REVEAL;
                flow->timer = 1.0f * anim_m;
                break;
            }

            /* Normal cleanup (winner, spectator, return) */
            rs->sync_needed = true;
            /* Reset z_orders and face_up */
            if (flow->duel_staged_cv_idx >= 0 &&
                flow->duel_staged_cv_idx < rs->card_count)
                rs->cards[flow->duel_staged_cv_idx].z_order = 0;
            if (flow->duel_own_cv_idx >= 0 &&
                flow->duel_own_cv_idx < rs->card_count) {
                rs->cards[flow->duel_own_cv_idx].z_order = 0;
                rs->cards[flow->duel_own_cv_idx].face_up = false;
            }
            /* Clear fields */
            flow->duel_winner = -1;
            flow->duel_target_player = -1;
            flow->duel_target_card_idx = -1;
            flow->duel_own_card_idx = -1;
            flow->duel_returned = false;
            flow->duel_staged_cv_idx = -1;
            rs->staged_duel_cv_idx = -1;
            rs->staged_duel_own_cv_idx = -1;
            flow->duel_own_cv_idx = -1;
            flow->duel_ai_decided = false;
            flow->duel_watching = false;
            flow->duel_just_ended = true;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = (FLOW_BETWEEN_TRICKS_TIME + 1.5f) * anim_m;
        }
        break;
    }

    case FLOW_DUEL_ANIM_RETURN: {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        if (flow->timer <= 0.0f) {
            /* Clear face_up */
            if (flow->duel_staged_cv_idx >= 0 &&
                flow->duel_staged_cv_idx < rs->card_count) {
                rs->cards[flow->duel_staged_cv_idx].face_up = false;
                rs->cards[flow->duel_staged_cv_idx].z_order = 0;
            }
            /* Clear fields */
            flow->duel_winner = -1;
            flow->duel_target_player = -1;
            flow->duel_target_card_idx = -1;
            flow->duel_own_card_idx = -1;
            flow->duel_returned = false;
            flow->duel_staged_cv_idx = -1;
            rs->staged_duel_cv_idx = -1;
            rs->staged_duel_own_cv_idx = -1;
            flow->duel_own_cv_idx = -1;
            flow->duel_ai_decided = false;
            flow->duel_watching = false;
            flow->duel_just_ended = true;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = (FLOW_BETWEEN_TRICKS_TIME + 1.5f) * anim_m;
        }
        break;
    }

    case FLOW_DUEL_RECEIVE_REVEAL: {
        /* Victim: card revealed at staging, hold for viewing */
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        if (flow->timer <= 0.0f) {
            /* Animate card into hand slot */
            if (flow->duel_own_cv_idx >= 0 &&
                flow->duel_own_cv_idx < rs->card_count) {
                int slot = flow->duel_target_card_idx;
                Vector2 positions[MAX_HAND_SIZE];
                float rotations[MAX_HAND_SIZE];
                int count = 0;
                layout_hand_positions(POS_BOTTOM,
                                      gs->players[0].hand.count,
                                      &rs->layout, positions, rotations,
                                      &count);
                Vector2 dest = (slot >= 0 && slot < count)
                    ? positions[slot] : positions[count / 2];
                float rot = (slot >= 0 && slot < count)
                    ? rotations[slot] : rotations[count / 2];
                anim_start(&rs->cards[flow->duel_own_cv_idx], dest, rot,
                           ANIM_PASS_RECEIVE_DURATION, EASE_OUT_BACK);
            }
            flow->step = FLOW_DUEL_RECEIVE;
            flow->timer = ANIM_PASS_RECEIVE_DURATION * anim_m;
        }
        break;
    }

    case FLOW_DUEL_RECEIVE: {
        /* Victim: card landed in hand, cleanup */
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        if (flow->timer <= 0.0f) {
            if (flow->duel_own_cv_idx >= 0 &&
                flow->duel_own_cv_idx < rs->card_count) {
                rs->cards[flow->duel_own_cv_idx].z_order = 0;
                rs->cards[flow->duel_own_cv_idx].face_up = false;
            }
            rs->sync_needed = true;
            /* Clear all duel fields */
            flow->duel_winner = -1;
            flow->duel_target_player = -1;
            flow->duel_target_card_idx = -1;
            flow->duel_own_card_idx = -1;
            flow->duel_returned = false;
            flow->duel_staged_cv_idx = -1;
            rs->staged_duel_cv_idx = -1;
            rs->staged_duel_own_cv_idx = -1;
            flow->duel_own_cv_idx = -1;
            flow->duel_ai_decided = false;
            flow->duel_watching = false;
            flow->duel_just_ended = true;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = (FLOW_BETWEEN_TRICKS_TIME + 1.5f) * anim_m;
        }
        break;
    }

    case FLOW_BETWEEN_TRICKS:
        if (flow->timer <= 0.0f) {
            rs->trick_anim_in_progress = false;
            flow->has_saved_trick = false;
            /* After a duel, reset to 0 so IDLE re-detects all cards in the
             * new trick and animates them (state was consumed during duel). */
            if (flow->duel_just_ended) {
                flow->prev_trick_count = 0;
                flow->duel_just_ended = false;
            } else {
                flow->prev_trick_count = gs->current_trick.num_played;
            }
            rs->trick_visible_count = 0; /* reset cap — sync shows current state */
            rs->sync_needed = true; /* Re-sync now that trick data is unprotected */
            flow->rogue_effect_handled = false;
            flow->duel_effect_handled = false;
            flow->step = FLOW_IDLE;

        }
        break;
    }
}
