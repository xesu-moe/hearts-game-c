/* ============================================================
 * @deps-implements: pass_phase.h
 * @deps-requires: pass_phase.h (PassPhaseState with prev_draft_round), core/game_state.h,
 *                 core/hand.h, core/settings.h, core/input_cmd.h (InputCmd),
 *                 render/anim.h, render/layout.h, render/render.h,
 *                 phase2/phase2_state.h, phase2/contract_logic.h, assert.h
 * @deps-last-changed: 2026-04-01
 * ============================================================ */

#include "pass_phase.h"

#include <assert.h>
#include <stdio.h>

#include "core/hand.h"
#include "core/input_cmd.h"
#include "core/settings.h"
#include "render/anim.h"
#include "render/layout.h"
#include "render/render.h"
#include "phase2/contract_logic.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

float pass_subphase_time_limit(PassSubphase sub, float bonus)
{
    switch (sub) {
    case PASS_SUB_DEALER:    return PASS_DEALER_TIME + bonus;
    case PASS_SUB_CONTRACT:  return PASS_CONTRACT_TIME + bonus;
    case PASS_SUB_CARD_PASS: return PASS_CARD_PASS_TIME + bonus;
    case PASS_SUB_TOSS_ANIM: return 0.0f;  /* driven by animation completion */
    case PASS_SUB_TOSS_WAIT: return ANIM_PASS_WAIT_DURATION * anim_get_speed();
    case PASS_SUB_REVEAL:    return PASS_REVEAL_DURATION * anim_get_speed();
    case PASS_SUB_RECEIVE:   return 0.0f;  /* driven by animation completion */
    default: return 0.0f;
    }
    return 0.0f;
}

/* ---- Dealer determination ---- */

int dealer_determine_player(const int prev_round_points[NUM_PLAYERS],
                            const GameState *gs)
{
    if (gs->round_number <= 1) return -1;

    int best = -1;
    int best_pts = -1;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (prev_round_points[i] > best_pts) {
            best_pts = prev_round_points[i];
            best = i;
        } else if (prev_round_points[i] == best_pts && best >= 0) {
            /* Break tie by total score, then lowest id */
            int ti = gs->players[i].total_score;
            int tb = gs->players[best].total_score;
            if (ti > tb || (ti == tb && i < best))
                best = i;
        }
    }
    return best;
}

void setup_dealer_ui(PassPhaseState *pps, RenderState *rs, int dealer_player_id)
{
    (void)dealer_player_id;
    rs->dealer_ui_active = true;
    rs->dealer_selected_dir = pps->dealer_dir;
    /* Map amount to button index */
    rs->dealer_selected_amt = -1;
    for (int i = 0; i < DEALER_AMOUNT_COUNT; i++) {
        if (DEALER_AMOUNTS[i] == pps->dealer_amt) {
            rs->dealer_selected_amt = i;
            break;
        }
    }
}

static char s_dealer_announce[64];

void dealer_announce(PassPhaseState *pps, RenderState *rs)
{
    int amt = pps->dealer_amt;
    if (amt == 0) {
        snprintf(s_dealer_announce, sizeof(s_dealer_announce),
                 "No passing this round!");
    } else {
        const char *dir_name = "left";
        switch (pps->dealer_dir) {
        case PASS_LEFT:   dir_name = "the left";  break;
        case PASS_RIGHT:  dir_name = "the right"; break;
        case PASS_ACROSS: dir_name = "the front"; break;
        default: break;
        }
        snprintf(s_dealer_announce, sizeof(s_dealer_announce),
                 "Pass %d card%s to %s!", amt, amt == 1 ? "" : "s", dir_name);
    }
    rs->pass_status_text = s_dealer_announce;
    rs->dealer_ui_active = false;
    render_chat_log_push_color(rs, s_dealer_announce, YELLOW);
    pps->dealer_ui_active = false;
    pps->dealer_announced = true;
    pps->dealer_announce_timer = 0.0f;
}

/* Draft status text buffers (static to keep pointer valid for pass_status_text) */
static char s_draft_status[96];

void setup_draft_ui(RenderState *rs, Phase2State *p2)
{
    DraftState *draft = &p2->round.draft;
    DraftPlayerState *ps = &draft->players[0]; /* human player */

    int count = ps->available_count;
    if (count > DRAFT_GROUP_SIZE) count = DRAFT_GROUP_SIZE;

    int ids[DRAFT_GROUP_SIZE];
    const char *names[DRAFT_GROUP_SIZE];
    const char *descs[DRAFT_GROUP_SIZE];

    for (int i = 0; i < count; i++) {
        int cid = ps->available[i].contract_id;
        int tid = ps->available[i].transmutation_id;
        ids[i] = cid;
        rs->draft_transmute_ids[i] = tid;

        const ContractDef *cd = phase2_get_contract(cid);
        const TransmutationDef *td = (tid >= 0) ? phase2_get_transmutation(tid) : NULL;

        /* Label = contract condition description (not name) */
        if (cd) {
            snprintf(rs->draft_btn_labels[i], sizeof(rs->draft_btn_labels[i]),
                     "%s", cd->description);
        } else {
            snprintf(rs->draft_btn_labels[i], sizeof(rs->draft_btn_labels[i]),
                     "Unknown");
        }

        /* Subtitle = transmutation name + description */
        if (td) {
            snprintf(rs->draft_btn_subtitles[i], sizeof(rs->draft_btn_subtitles[i]),
                     "%s\n%s", td->name, td->description);
        } else {
            rs->draft_btn_subtitles[i][0] = '\0';
        }

        names[i] = rs->draft_btn_labels[i];
        descs[i] = rs->draft_btn_subtitles[i];
    }
    /* Clear unused slots */
    for (int i = count; i < DRAFT_GROUP_SIZE; i++)
        rs->draft_transmute_ids[i] = -1;

    rs->draft_round_display = draft->current_round + 1;
    rs->draft_picks_made = ps->pick_count;

    /* Build status text with round info */
    int round = draft->current_round + 1;
    int pass_count = count - 1;
    if (round < DRAFT_ROUNDS) {
        snprintf(s_draft_status, sizeof(s_draft_status),
                 "Draft Round %d/3 - Pick 1, pass %d to the left",
                 round, pass_count);
    } else {
        snprintf(s_draft_status, sizeof(s_draft_status),
                 "Draft Round %d/3 - Pick 1, discard the last",
                 round);
    }
    rs->pass_status_text = s_draft_status;

    render_set_contract_options(rs, ids, count, names, descs);
}

void advance_pass_subphase(PassPhaseState *pps, GameState *gs,
                           RenderState *rs, Phase2State *p2,
                           PassSubphase next)
{
    pps->subphase = next;
    pps->timer = 0.0f;
    pps->draft_pick_pending = false;
    pps->draft_click_consumed = false;
    rs->pass_subphase = next;
    rs->pass_subphase_remaining = pass_subphase_time_limit(next, pps->timer_bonus);
    rs->pass_status_text = NULL;

    switch (next) {
    case PASS_SUB_TOSS_ANIM:
    case PASS_SUB_TOSS_WAIT:
    case PASS_SUB_REVEAL:
    case PASS_SUB_RECEIVE:
    default:
        break;  /* animation/server-driven subphases don't need setup here */
    case PASS_SUB_DEALER: {
        int dp = gs->players[0].total_score; /* dummy — actual dealer id from phase_transitions */
        (void)dp;
        pps->dealer_dir = PASS_LEFT;
        pps->dealer_amt = DEFAULT_PASS_CARD_COUNT;
        pps->dealer_ui_active = false;
        pps->ai_dealer_pending = false;
        /* Actual UI setup done by phase_transitions after calling advance */
        break;
    }
    case PASS_SUB_CONTRACT:
        /* Generate draft pool on first entry */
        if (!p2->round.draft.active) {
            draft_generate_pool(&p2->round.draft);
            /* AI players pick instantly for round 0 */
            for (int p = 1; p < NUM_PLAYERS; p++)
                draft_ai_pick(&p2->round.draft, p);
        }
        setup_draft_ui(rs, p2);
        break;
    case PASS_SUB_CARD_PASS:
        rs->contract_ui_active = false;
        rs->pass_status_text = NULL;
        /* Skip card pass if dealer chose amount=0 */
        if (gs->pass_card_count == 0) {
            gs->lead_player = game_state_find_two_of_clubs(gs);
            trick_init(&gs->current_trick, gs->lead_player);
            gs->phase = PHASE_PLAYING;
            rs->sync_needed = true;
            return;
        }
        /* AI players apply transmutations at card pass start */
        if (p2->enabled) {
            for (int p = 1; p < NUM_PLAYERS; p++) {
                transmute_ai_apply(&gs->players[p].hand,
                                   &p2->players[p].hand_transmutes,
                                   &p2->players[p].transmute_inv, true, p);
            }
        }
        break;
    }
}

void auto_select_human_pass(GameState *gs, RenderState *rs)
{
    const Hand *hand = &gs->players[0].hand;
    assert(hand->count >= gs->pass_card_count);
    Card pass_cards[MAX_PASS_CARD_COUNT];
    for (int i = 0; i < gs->pass_card_count; i++) {
        pass_cards[i] = hand->cards[i];
    }
    game_state_select_pass(gs, 0, pass_cards, gs->pass_card_count);
    render_clear_selection(rs);
}

/* ---- Pass animation helpers ---- */

static int pass_dest_player(int from, PassDirection dir)
{
    static const int offsets[PASS_COUNT] = {1, 3, 2, 0};
    return (from + offsets[dir]) % NUM_PLAYERS;
}

/* ---- Pass animation (anti-cheat safe) ---- */

void pass_start_toss_anim_batched(PassPhaseState *pps, GameState *gs,
                                 RenderState *rs,
                                 const Card *received_cards, int received_count)
{
    const LayoutConfig *cfg = &rs->layout;

    /* Save received cards for reveal phase */
    if (received_count > MAX_PASS_CARD_COUNT)
        received_count = MAX_PASS_CARD_COUNT;
    pps->received_count = received_count;
    for (int i = 0; i < received_count; i++)
        pps->received_cards[i] = received_cards[i];

    rs->pass_staged_count = 0;
    rs->pass_anim_in_progress = true;
    pps->pass_anim = true;

    int opponent_to_human_count = 0; /* tracks recv card index assignment */

    for (int p = 0; p < NUM_PLAYERS; p++) {
        int dest = pass_dest_player(p, gs->pass_direction);
        PlayerPosition dest_spos = (PlayerPosition)dest;

        /* Compute start positions */
        Vector2 human_starts[MAX_PASS_CARD_COUNT];
        float   human_rots[MAX_PASS_CARD_COUNT];
        Vector2 ai_start = layout_board_center(cfg);
        float   ai_start_rot = 0.0f;

        if (p == 0) {
            /* Human: each card starts from its own hand position.
             * Match using pass_selection_hints to disambiguate same
             * suit+rank cards (normal vs transmuted). */
            bool claimed[MAX_HAND_SIZE] = {false};
            for (int j = 0; j < gs->pass_card_count; j++) {
                human_starts[j] = layout_board_center(cfg);
                human_rots[j] = 0.0f;
                int hint = gs->pass_selection_hints[0][j];
                /* First pass: exact match (card + hint) */
                for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
                    if (claimed[i]) continue;
                    int vi = rs->hand_visuals[0][i];
                    if (vi >= 0 && vi < rs->card_count &&
                        card_equals(rs->cards[vi].card,
                                    gs->pass_selections[0][j]) &&
                        rs->hand_transmute_ids[i] == hint) {
                        human_starts[j] = rs->cards[vi].position;
                        human_rots[j] = rs->cards[vi].rotation;
                        claimed[i] = true;
                        break;
                    }
                }
            }
        } else {
            /* Opponent: use hand midpoint */
            if (rs->hand_visual_counts[p] > 0) {
                int mid = rs->hand_visual_counts[p] / 2;
                int mid_idx = rs->hand_visuals[p][mid];
                if (mid_idx >= 0 && mid_idx < rs->card_count) {
                    const CardVisual *mid_cv = &rs->cards[mid_idx];
                    ai_start = (Vector2){
                        mid_cv->position.x - mid_cv->origin.x,
                        mid_cv->position.y - mid_cv->origin.y
                    };
                    ai_start_rot = mid_cv->rotation;
                }
            }
        }

        for (int j = 0; j < gs->pass_card_count; j++) {
            Vector2 start_pos = (p == 0) ? human_starts[j] : ai_start;
            float   start_rot = (p == 0) ? human_rots[j]   : ai_start_rot;

            Vector2 target = layout_pass_staging_position(
                dest_spos, j, gs->pass_card_count, cfg);

            float delay = (float)p * PASS_PLAYER_STAGGER +
                          (float)j * PASS_TOSS_STAGGER;

            int idx = render_alloc_card_visual(rs);
            if (idx < 0) continue;

            CardVisual *cv = &rs->cards[idx];
            /* Human: use actual card identity; opponents: dummy (rendered face-down) */
            if (p == 0)
                cv->card = gs->pass_selections[0][j];
            else
                cv->card = (Card){.suit = -1, .rank = -1};
            cv->face_up = false;
            cv->scale = 0.7f * cfg->scale;
            cv->opacity = 1.0f;
            cv->z_order = 30 + rs->pass_staged_count;
            cv->origin = (Vector2){
                CARD_WIDTH_REF * cv->scale * 0.5f,
                CARD_HEIGHT_REF * cv->scale * 0.5f
            };

            anim_setup_toss(cv, start_pos, start_rot, target,
                            NULL, ANIM_PASS_TOSS_DURATION, delay);

            /* For cards destined for human (dest==0), assign received card
             * identity so reveal can flip them face-up with correct art */
            PassStagedCard *sc = &rs->pass_staged[rs->pass_staged_count];
            sc->card_visual_idx = idx;
            sc->from_player = p;
            sc->to_player = dest;
            if (dest == 0 && p != 0) {
                /* Map opponent→human staged cards to received cards */
                if (opponent_to_human_count < received_count) {
                    sc->card = received_cards[opponent_to_human_count];
                    cv->card = received_cards[opponent_to_human_count];
                } else {
                    sc->card = (Card){.suit = -1, .rank = -1};
                }
                opponent_to_human_count++;
            } else if (p == 0) {
                sc->card = gs->pass_selections[0][j];
            } else {
                sc->card = (Card){.suit = -1, .rank = -1};
            }
            rs->pass_staged_count++;
        }
    }

    /* Remove tossed cards from hand visuals (human only — we know the cards).
     * Match using pass_selection_hints to hide the correct visual
     * when two cards share suit+rank (normal + transmuted). */
    {
        bool hide_claimed[MAX_HAND_SIZE] = {false};
        for (int j = 0; j < gs->pass_card_count; j++) {
            Card pass_card = gs->pass_selections[0][j];
            int hint = gs->pass_selection_hints[0][j];
            for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
                if (hide_claimed[i]) continue;
                int idx = rs->hand_visuals[0][i];
                if (card_equals(rs->cards[idx].card, pass_card) &&
                    rs->hand_transmute_ids[i] == hint) {
                    rs->cards[idx].opacity = 0.0f;
                    hide_claimed[i] = true;
                    break;
                }
            }
        }
    }
    /* Opponents: hide pass_card_count cards from each hand visual */
    for (int p = 1; p < NUM_PLAYERS; p++) {
        int hidden = 0;
        for (int i = rs->hand_visual_counts[p] - 1;
             i >= 0 && hidden < gs->pass_card_count; i--) {
            int idx = rs->hand_visuals[p][i];
            if (rs->cards[idx].opacity > 0.0f) {
                rs->cards[idx].opacity = 0.0f;
                hidden++;
            }
        }
    }

    /* Human hand: smoothly slide remaining cards to close gaps */
    {
        int remaining[MAX_HAND_SIZE];
        int remain_count = 0;
        for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
            int idx = rs->hand_visuals[0][i];
            if (rs->cards[idx].opacity > 0.0f)
                remaining[remain_count++] = idx;
        }
        Vector2 positions[MAX_HAND_SIZE];
        float rotations[MAX_HAND_SIZE];
        int count = 0;
        layout_hand_positions(POS_BOTTOM, remain_count, cfg,
                              positions, rotations, &count);
        for (int i = 0; i < remain_count; i++) {
            CardVisual *cv = &rs->cards[remaining[i]];
            anim_start(cv, positions[i], rotations[i],
                       ANIM_PASS_HAND_SLIDE_DURATION, EASE_OUT_QUAD);
        }
    }

    pps->subphase = PASS_SUB_TOSS_ANIM;
    pps->timer = 0.0f;
    rs->pass_subphase = PASS_SUB_TOSS_ANIM;
    rs->pass_subphase_remaining = 0.0f;
    rs->pass_status_text = NULL;
}

void pass_start_single_toss(PassPhaseState *pps, GameState *gs,
                                   RenderState *rs, int seat)
{
    const LayoutConfig *cfg = &rs->layout;

    /* First call: initialize staging state */
    if (!pps->async_toss) {
        rs->pass_staged_count = 0;
        rs->pass_anim_in_progress = true;
        pps->async_toss = true;
    }

    /* Already tossed for this seat */
    if (pps->toss_started[seat]) return;

    int dest = pass_dest_player(seat, gs->pass_direction);
    PlayerPosition dest_spos = (PlayerPosition)dest;

    /* Compute start positions */
    Vector2 starts[MAX_PASS_CARD_COUNT];
    float   rots[MAX_PASS_CARD_COUNT];

    if (seat == 0) {
        /* Human: each card starts from its own hand visual position.
         * Match using pass_selection_hints to disambiguate same
         * suit+rank cards (normal vs transmuted). */
        bool claimed[MAX_HAND_SIZE] = {false};
        for (int j = 0; j < gs->pass_card_count; j++) {
            starts[j] = layout_board_center(cfg);
            rots[j] = 0.0f;
            int hint = gs->pass_selection_hints[0][j];
            for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
                if (claimed[i]) continue;
                int vi = rs->hand_visuals[0][i];
                if (vi >= 0 && vi < rs->card_count &&
                    card_equals(rs->cards[vi].card,
                                gs->pass_selections[0][j]) &&
                    rs->hand_transmute_ids[i] == hint) {
                    starts[j] = rs->cards[vi].position;
                    rots[j] = rs->cards[vi].rotation;
                    claimed[i] = true;
                    break;
                }
            }
        }
    } else {
        /* Opponent: use hand midpoint */
        Vector2 mid_pos = layout_board_center(cfg);
        float mid_rot = 0.0f;
        if (rs->hand_visual_counts[seat] > 0) {
            int mid = rs->hand_visual_counts[seat] / 2;
            int mid_idx = rs->hand_visuals[seat][mid];
            if (mid_idx >= 0 && mid_idx < rs->card_count) {
                const CardVisual *mid_cv = &rs->cards[mid_idx];
                mid_pos = (Vector2){
                    mid_cv->position.x - mid_cv->origin.x,
                    mid_cv->position.y - mid_cv->origin.y
                };
                mid_rot = mid_cv->rotation;
            }
        }
        for (int j = 0; j < gs->pass_card_count; j++) {
            starts[j] = mid_pos;
            rots[j] = mid_rot;
        }
    }

    /* Create staged card visuals for this player */
    for (int j = 0; j < gs->pass_card_count; j++) {
        Vector2 target = layout_pass_staging_position(
            dest_spos, j, gs->pass_card_count, cfg);

        /* No inter-player stagger (they fire at different real times),
         * only per-card stagger within one player */
        float delay = (float)j * PASS_TOSS_STAGGER;

        int idx = render_alloc_card_visual(rs);
        if (idx < 0) continue;

        CardVisual *cv = &rs->cards[idx];
        /* Human: actual card identity; opponents: dummy (face-down) */
        if (seat == 0)
            cv->card = gs->pass_selections[0][j];
        else
            cv->card = (Card){.suit = -1, .rank = -1};
        cv->face_up = false;
        cv->scale = 0.7f * cfg->scale;
        cv->opacity = 1.0f;
        cv->z_order = 30 + rs->pass_staged_count;
        cv->origin = (Vector2){
            CARD_WIDTH_REF * cv->scale * 0.5f,
            CARD_HEIGHT_REF * cv->scale * 0.5f
        };

        anim_setup_toss(cv, starts[j], rots[j], target,
                        NULL, ANIM_PASS_TOSS_DURATION, delay);

        PassStagedCard *sc = &rs->pass_staged[rs->pass_staged_count];
        sc->card_visual_idx = idx;
        sc->from_player = seat;
        sc->to_player = dest;
        /* Card identity: human's own cards are known, everything else is
         * a dummy. Received card identities for human are assigned later
         * via pass_assign_received_cards() when the server state
         * arrives. */
        if (seat == 0)
            sc->card = gs->pass_selections[0][j];
        else
            sc->card = (Card){.suit = -1, .rank = -1};
        rs->pass_staged_count++;
    }

    /* Remove tossed cards from hand visuals.
     * Match using pass_selection_hints to hide the correct visual
     * when two cards share suit+rank (normal + transmuted). */
    if (seat == 0) {
        bool hide_claimed[MAX_HAND_SIZE] = {false};
        for (int j = 0; j < gs->pass_card_count; j++) {
            Card pass_card = gs->pass_selections[0][j];
            int hint = gs->pass_selection_hints[0][j];
            for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
                if (hide_claimed[i]) continue;
                int vi = rs->hand_visuals[0][i];
                if (card_equals(rs->cards[vi].card, pass_card) &&
                    rs->hand_transmute_ids[i] == hint) {
                    rs->cards[vi].opacity = 0.0f;
                    hide_claimed[i] = true;
                    break;
                }
            }
        }
        /* Slide remaining human hand cards to close gaps */
        int remaining[MAX_HAND_SIZE];
        int remain_count = 0;
        for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
            int vi = rs->hand_visuals[0][i];
            if (rs->cards[vi].opacity > 0.0f)
                remaining[remain_count++] = vi;
        }
        Vector2 positions[MAX_HAND_SIZE];
        float rotations[MAX_HAND_SIZE];
        int count = 0;
        layout_hand_positions(POS_BOTTOM, remain_count, cfg,
                              positions, rotations, &count);
        for (int i = 0; i < remain_count; i++) {
            CardVisual *cv = &rs->cards[remaining[i]];
            anim_start(cv, positions[i], rotations[i],
                       ANIM_PASS_HAND_SLIDE_DURATION, EASE_OUT_QUAD);
        }
    } else {
        /* Opponent: hide pass_card_count cards from end of hand */
        int hidden = 0;
        for (int i = rs->hand_visual_counts[seat] - 1;
             i >= 0 && hidden < gs->pass_card_count; i--) {
            int vi = rs->hand_visuals[seat][i];
            if (rs->cards[vi].opacity > 0.0f) {
                rs->cards[vi].opacity = 0.0f;
                hidden++;
            }
        }
    }

    pps->toss_started[seat] = true;
    pps->toss_count++;

    /* Enter toss anim subphase once the human has tossed.
     * AI seats may toss before the human — keep CARD_PASS so
     * the human can still select and confirm their pass cards. */
    if (pps->subphase == PASS_SUB_CARD_PASS && pps->toss_started[0]) {
        pps->subphase = PASS_SUB_TOSS_ANIM;
        pps->timer = 0.0f;
        rs->pass_subphase = PASS_SUB_TOSS_ANIM;
        rs->pass_subphase_remaining = 0.0f;
        rs->pass_status_text = NULL;
    }
}

void pass_assign_received_cards(PassPhaseState *pps, RenderState *rs,
                                       const Card *received, int count)
{
    /* Store received cards for reveal phase */
    if (count > MAX_PASS_CARD_COUNT)
        count = MAX_PASS_CARD_COUNT;
    pps->received_count = count;
    for (int i = 0; i < count; i++)
        pps->received_cards[i] = received[i];

    /* Assign identities to staged cards destined for human (from opponents) */
    int recv_idx = 0;
    for (int i = 0; i < rs->pass_staged_count && recv_idx < count; i++) {
        PassStagedCard *sc = &rs->pass_staged[i];
        if (sc->to_player == 0 && sc->from_player != 0) {
            sc->card = received[recv_idx];
            int vi = sc->card_visual_idx;
            if (vi >= 0 && vi < rs->card_count)
                rs->cards[vi].card = received[recv_idx];
            recv_idx++;
        }
    }
}

bool pass_toss_animations_done(const RenderState *rs)
{
    for (int i = 0; i < rs->pass_staged_count; i++) {
        int idx = rs->pass_staged[i].card_visual_idx;
        if (idx >= 0 && idx < rs->card_count && rs->cards[idx].animating) {
            return false;
        }
    }
    return rs->pass_staged_count > 0;
}

bool pass_receive_animations_done(const RenderState *rs)
{
    for (int i = 0; i < rs->pass_staged_count; i++) {
        int idx = rs->pass_staged[i].card_visual_idx;
        if (idx >= 0 && idx < rs->card_count && rs->cards[idx].animating) {
            return false;
        }
    }
    return true;
}

/* ---- Online receive: no execute_pass, hand is already post-pass ---- */

void pass_start_receive_anim(PassPhaseState *pps, GameState *gs,
                                    RenderState *rs,
                                    const GameSettings *settings)
{
    const LayoutConfig *cfg = &rs->layout;

    /* ---- Human hand: slide existing cards to open gaps for received ---- */
    {
        const Hand *hhand = &gs->players[0].hand;
        int full_count = hhand->count; /* 13 after server execute_pass */

        /* Identify which slots hold received cards */
        Card *received = pps->received_cards;
        int recv_count = pps->received_count;

        bool is_gap[MAX_HAND_SIZE] = {false};
        if (settings->auto_sort_received) {
            bool used[MAX_PASS_CARD_COUNT] = {false};
            for (int k = 0; k < full_count; k++) {
                for (int r = 0; r < recv_count; r++) {
                    if (!used[r] && card_equals(hhand->cards[k], received[r])) {
                        is_gap[k] = true;
                        used[r] = true;
                        break;
                    }
                }
            }
        } else {
            int gap_start = (full_count > recv_count) ? full_count - recv_count : 0;
            for (int k = gap_start; k < full_count; k++)
                is_gap[k] = true;
        }

        Vector2 positions13[MAX_HAND_SIZE];
        float rotations13[MAX_HAND_SIZE];
        int count13 = 0;
        layout_hand_positions(POS_BOTTOM, full_count, cfg,
                              positions13, rotations13, &count13);

        /* Existing hand visuals → non-gap slots */
        int existing_vis[MAX_HAND_SIZE];
        int existing_count = 0;
        for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
            int idx = rs->hand_visuals[0][i];
            if (idx >= 0 && idx < rs->card_count &&
                rs->cards[idx].opacity > 0.0f)
                existing_vis[existing_count++] = idx;
        }

        int vis_i = 0;
        for (int slot = 0; slot < count13 && vis_i < existing_count; slot++) {
            if (is_gap[slot]) continue;
            CardVisual *cv = &rs->cards[existing_vis[vis_i]];
            anim_start(cv, positions13[slot], rotations13[slot],
                       ANIM_PASS_HAND_SLIDE_DURATION, EASE_OUT_QUAD);
            vis_i++;
        }

        /* Animate staged cards into gap slots */
        int human_recv_idx = 0;
        int gap_slots[MAX_PASS_CARD_COUNT];
        int gap_count = 0;
        for (int k = 0; k < count13 && gap_count < MAX_PASS_CARD_COUNT; k++) {
            if (is_gap[k]) gap_slots[gap_count++] = k;
        }

        for (int i = 0; i < rs->pass_staged_count; i++) {
            PassStagedCard *sc = &rs->pass_staged[i];
            if (sc->to_player != 0) continue;
            int idx = sc->card_visual_idx;
            if (idx < 0 || idx >= rs->card_count) continue;

            CardVisual *cv = &rs->cards[idx];
            int slot = (human_recv_idx < gap_count)
                           ? gap_slots[human_recv_idx] : count13 - 1;
            anim_start(cv, positions13[slot], rotations13[slot],
                       ANIM_PASS_RECEIVE_DURATION, EASE_OUT_BACK);
            cv->anim_delay = ANIM_PASS_RECEIVE_GAP_DELAY * anim_get_speed();
            human_recv_idx++;
        }
    }

    /* ---- Opponent staged cards: fly to hand center ---- */
    for (int i = 0; i < rs->pass_staged_count; i++) {
        PassStagedCard *sc = &rs->pass_staged[i];
        if (sc->to_player == 0) continue;
        int idx = sc->card_visual_idx;
        if (idx < 0 || idx >= rs->card_count) continue;

        CardVisual *cv = &rs->cards[idx];
        PlayerPosition dest_spos = (PlayerPosition)sc->to_player;
        int hand_count = gs->players[sc->to_player].hand.count;
        if (hand_count < 1) hand_count = 13;
        Vector2 positions[MAX_HAND_SIZE];
        float rotations[MAX_HAND_SIZE];
        int count = 0;
        layout_hand_positions(dest_spos, hand_count, cfg,
                              positions, rotations, &count);
        int mid = count / 2;
        anim_start(cv, positions[mid], rotations[mid],
                   ANIM_PASS_RECEIVE_DURATION, EASE_OUT_BACK);
    }

    pps->subphase = PASS_SUB_RECEIVE;
    pps->timer = 0.0f;
    rs->pass_subphase = PASS_SUB_RECEIVE;
    rs->pass_subphase_remaining = 0.0f;
    rs->pass_status_text = NULL;
}

/* ---- Reveal: animate human-destined cards to preview row ---- */

static void pass_start_reveal(PassPhaseState *pps, GameState *gs,
                              RenderState *rs)
{
    const LayoutConfig *cfg = &rs->layout;
    float human_scale = cfg->scale; /* full-size (rel_scale = 1.0) */

    /* Compute preview row positions for human-destined cards */
    Vector2 preview_pos[MAX_PASS_CARD_COUNT];
    layout_pass_preview_positions(gs->pass_card_count, cfg, preview_pos);

    /* Human hand origin: bottom-center at full scale */
    float cw_s = cfg->card_width;
    float ch_s = cfg->card_height;
    Vector2 hand_origin = {cw_s * 0.5f, ch_s};

    int human_idx = 0;
    for (int i = 0; i < rs->pass_staged_count; i++) {
        PassStagedCard *sc = &rs->pass_staged[i];
        if (sc->to_player != 0) continue;
        int idx = sc->card_visual_idx;
        if (idx < 0 || idx >= rs->card_count) continue;

        CardVisual *cv = &rs->cards[idx];
        cv->face_up = true;
        cv->z_order = 300 + human_idx;

        /* Compensate position for origin change (center → bottom-center)
         * so the card doesn't visually jump when origin is set.
         * Old origin was center-pivot at current scale. */
        Vector2 old_origin = cv->origin;
        Vector2 delta = {hand_origin.x - old_origin.x,
                         hand_origin.y - old_origin.y};
        cv->position.x -= delta.x;
        cv->position.y -= delta.y;
        cv->origin = hand_origin;

        anim_start_scaled(cv, preview_pos[human_idx], 0.0f,
                          human_scale, ANIM_PASS_REVEAL_FLY_DURATION,
                          EASE_OUT_BACK);
        human_idx++;
    }

    pps->subphase = PASS_SUB_REVEAL;
    pps->timer = 0.0f;
    rs->pass_subphase = PASS_SUB_REVEAL;
    rs->pass_subphase_remaining = pass_subphase_time_limit(PASS_SUB_REVEAL, pps->timer_bonus);
    rs->pass_status_text = NULL;
}

static bool pass_reveal_animations_done(const RenderState *rs)
{
    for (int i = 0; i < rs->pass_staged_count; i++) {
        if (rs->pass_staged[i].to_player != 0) continue;
        int idx = rs->pass_staged[i].card_visual_idx;
        if (idx >= 0 && idx < rs->card_count && rs->cards[idx].animating) {
            return false;
        }
    }
    return true;
}

/* ---- Subphase handlers ---- */

void draft_finish_round(PassPhaseState *pps, GameState *gs,
                               RenderState *rs, Phase2State *p2)
{
    DraftState *draft = &p2->round.draft;

    draft_advance_round(draft);

    if (draft_is_complete(draft)) {
        draft_finalize(draft, p2);
        rs->contract_ui_active = false;

        if (gs->pass_direction == PASS_NONE) {
            gs->phase = PHASE_PLAYING;
            rs->sync_needed = true;
        } else {
            advance_pass_subphase(pps, gs, rs, p2, PASS_SUB_CARD_PASS);
        }
    } else {
        /* AI picks for the new round */
        for (int p = 1; p < NUM_PLAYERS; p++)
            draft_ai_pick(draft, p);

        setup_draft_ui(rs, p2);
        pps->timer = 0.0f;
        rs->pass_subphase_remaining = PASS_CONTRACT_TIME + pps->timer_bonus;
    }
}

void pass_subphase_update(PassPhaseState *pps, GameState *gs,
                          RenderState *rs, Phase2State *p2,
                          const GameSettings *settings, float dt)
{
    if (gs->phase != PHASE_PASSING && gs->phase != PHASE_SETTINGS) return;
    /* During PHASE_SETTINGS: only tick the timer so it stays accurate,
     * but don't process subphase transitions or AI logic. */
    if (gs->phase == PHASE_SETTINGS) {
        float limit = pass_subphase_time_limit(pps->subphase, pps->timer_bonus);
        pps->timer += dt;
        float remaining = limit - pps->timer;
        if (remaining < 0.0f) remaining = 0.0f;
        rs->pass_subphase_remaining = remaining;
        return;
    }

    float limit = pass_subphase_time_limit(pps->subphase, pps->timer_bonus);
    pps->timer += dt;
    float remaining = limit - pps->timer;
    if (remaining < 0.0f) remaining = 0.0f;
    rs->pass_subphase_remaining = remaining;
    rs->pass_subphase = pps->subphase;

    /* Detect draft round advance and reset timer per pick */
    if (pps->subphase == PASS_SUB_CONTRACT) {
        int cur_round = p2->round.draft.current_round;
        if (cur_round != pps->prev_draft_round) {
            pps->prev_draft_round = cur_round;
            pps->timer = 0.0f;
            rs->pass_subphase_remaining = PASS_CONTRACT_TIME + pps->timer_bonus;
            pps->draft_pick_pending = false;
        }
    }

    /* Timeout: auto-pick draft contract */
    if (pps->subphase == PASS_SUB_CONTRACT &&
        pps->timer >= PASS_CONTRACT_TIME + pps->timer_bonus && !pps->draft_pick_pending) {
        DraftState *draft = &p2->round.draft;
        if (draft->active && !draft->players[0].has_picked_this_round) {
            input_cmd_push((InputCmd){
                .type = INPUT_CMD_SELECT_CONTRACT,
                .source_player = 0,
                .contract = { .pair_index = 0 },
            });
            pps->draft_pick_pending = true;
        }
    }

    /* Timeout: auto-select pass cards and confirm (one-shot) */
    if (pps->subphase == PASS_SUB_CARD_PASS &&
        pps->timer >= PASS_CARD_PASS_TIME + pps->timer_bonus &&
        !gs->pass_ready[0] && !pps->pass_auto_sent) {
        int count = gs->pass_card_count;
        if (count > MAX_PASS_CARD_COUNT) count = MAX_PASS_CARD_COUNT;
        for (int i = 0; i < count && i < gs->players[0].hand.count; i++) {
            int hint_tid = -1;
            if (p2->enabled) {
                const HandTransmuteState *hts = &p2->players[0].hand_transmutes;
                if (transmute_is_transmuted(hts, i))
                    hint_tid = hts->slots[i].transmutation_id;
            }
            input_cmd_push((InputCmd){
                .type = INPUT_CMD_SELECT_CARD,
                .source_player = 0,
                .card = {
                    .card_index = hint_tid,
                    .card = gs->players[0].hand.cards[i],
                },
            });
        }
        input_cmd_push((InputCmd){ .type = INPUT_CMD_CONFIRM, .source_player = 0 });
        pps->pass_auto_sent = true;
    }

    /* Server drives DEALER, CONTRACT, CARD_PASS substates.
     * Animation substates (TOSS_ANIM..RECEIVE) run client-side when
     * pass_anim or async_toss is active. */
    if (!pps->pass_anim && !pps->async_toss)
        return;

    switch (pps->subphase) {
    case PASS_SUB_DEALER:
        break; /* server-driven */
    case PASS_SUB_CONTRACT:
        break; /* server-driven */
    case PASS_SUB_CARD_PASS:
        break; /* server-driven */
    case PASS_SUB_TOSS_ANIM:
        if (pps->async_toss) {
            /* Async mode: wait for ALL players to have tossed AND
             * all animations to finish before proceeding */
            if (pps->toss_count >= NUM_PLAYERS &&
                pass_toss_animations_done(rs)) {
                pps->subphase = PASS_SUB_TOSS_WAIT;
                pps->timer = 0.0f;
                rs->pass_subphase = PASS_SUB_TOSS_WAIT;
                rs->pass_wait_timer = 0.0f;
            }
        } else if (pass_toss_animations_done(rs)) {
            pps->subphase = PASS_SUB_TOSS_WAIT;
            pps->timer = 0.0f;
            rs->pass_subphase = PASS_SUB_TOSS_WAIT;
            rs->pass_wait_timer = 0.0f;
        }
        break;
    case PASS_SUB_TOSS_WAIT:
        rs->pass_wait_timer += dt;
        if (rs->pass_wait_timer >= pass_subphase_time_limit(PASS_SUB_TOSS_WAIT, pps->timer_bonus)) {
            pass_start_reveal(pps, gs, rs);
        }
        break;
    case PASS_SUB_REVEAL:
        if (pass_reveal_animations_done(rs)) {
            if (pps->timer >= pass_subphase_time_limit(PASS_SUB_REVEAL, pps->timer_bonus)) {
                pass_start_receive_anim(pps, gs, rs, settings);
            }
        }
        break;
    case PASS_SUB_RECEIVE:
        if (pass_receive_animations_done(rs)) {
            rs->pass_staged_count = 0;
            rs->pass_anim_in_progress = false;
            rs->sync_needed = true;
            /* Don't set phase locally — the deferred state
             * will be consumed now that anim is done */
            pps->pass_anim = false;
            pps->async_toss = false;
        }
        break;
    default:
        break;
    }
}

/* ---- Online pass UI sync ---- */

static const char *s_online_waiting = "Waiting for other players...";

void pass_sync_ui(PassPhaseState *pps, GameState *gs,
                         RenderState *rs, Phase2State *p2)
{
    if (gs->phase != PHASE_PASSING) return;

    rs->pass_subphase = pps->subphase;

    switch (pps->subphase) {
    case PASS_SUB_DEALER:
        if (pps->dealer_ui_active) {
            /* Only set up dealer UI once to avoid clobbering in-progress selection */
            if (!rs->dealer_ui_active) {
                setup_dealer_ui(pps, rs, 0);
                rs->pass_status_text = "Dealer: Choose pass direction and amount";
            }
            /* Continuously sync selections for visual feedback */
            rs->dealer_selected_dir = pps->dealer_dir;
            rs->dealer_selected_amt = -1;
            for (int i = 0; i < DEALER_AMOUNT_COUNT; i++) {
                if (DEALER_AMOUNTS[i] == pps->dealer_amt) {
                    rs->dealer_selected_amt = i;
                    break;
                }
            }
        } else {
            rs->dealer_ui_active = false;
            rs->pass_status_text = "Dealer is choosing...";
        }
        break;

    case PASS_SUB_CONTRACT:
        if (p2->enabled && p2->round.draft.active) {
            if (pps->draft_pick_pending ||
                p2->round.draft.players[0].has_picked_this_round) {
                rs->pass_status_text = s_online_waiting;
                if (rs->selected_contract_idx >= 0) {
                    rs->contract_ui_active = true;
                    /* Start fadeout once on entering wait */
                    if (!rs->draft_waiting)
                        rs->draft_fadeout_t = 1.0f;
                    rs->draft_waiting = true;
                } else {
                    rs->contract_ui_active = false;
                }
            } else {
                rs->draft_waiting = false;
                /* Only set up draft UI once per round to avoid flicker */
                if (!rs->contract_ui_active ||
                    rs->draft_round_display != p2->round.draft.current_round + 1) {
                    setup_draft_ui(rs, p2);
                }
            }
        }
        break;

    case PASS_SUB_CARD_PASS:
        rs->contract_ui_active = false;
        rs->draft_waiting = false;
        rs->pass_status_text = NULL;
        break;

    default:
        /* TOSS_ANIM, REVEAL, RECEIVE, TRANSMUTE — server-driven */
        rs->contract_ui_active = false;
        rs->draft_waiting = false;
        break;
    }
}
