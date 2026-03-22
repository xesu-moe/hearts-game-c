/* ============================================================
 * @deps-implements: pass_phase.h
 * @deps-requires: pass_phase.h, core/game_state.h, core/hand.h, core/settings.h,
 *                 ai.h, render/anim.h (anim_start_scaled, anim_get_speed, ANIM_PASS_HAND_SLIDE_DURATION, ANIM_PASS_RECEIVE_GAP_DELAY),
 *                 render/layout.h (layout_pass_preview_positions, layout_board_center, LayoutConfig),
 *                 render/render.h, phase2/phase2_state.h, phase2/contract_logic.h,
 *                 phase2/phase2_defs.h, phase2/vendetta_logic.h, phase2/transmutation_logic.h,
 *                 phase2/transmutation.h, assert.h, stdio.h
 * @deps-last-changed: 2026-03-22 — Hand slide timing: use ANIM_PASS_HAND_SLIDE_DURATION in pass_start_toss_anim/pass_start_receive_anim, ANIM_PASS_RECEIVE_GAP_DELAY in pass_start_receive_anim
 * ============================================================ */

#include "pass_phase.h"

#include <assert.h>
#include <stdio.h>

#include "ai.h"
#include "core/hand.h"
#include "core/settings.h"
#include "render/anim.h"
#include "render/layout.h"
#include "render/render.h"
#include "phase2/contract_logic.h"
#include "phase2/vendetta_logic.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

float pass_subphase_time_limit(PassSubphase sub)
{
    switch (sub) {
    case PASS_SUB_VENDETTA:  return PASS_VENDETTA_TIME;
    case PASS_SUB_CONTRACT:  return PASS_CONTRACT_TIME;
    case PASS_SUB_CARD_PASS: return PASS_CARD_PASS_TIME;
    case PASS_SUB_TOSS_ANIM: return 0.0f;  /* driven by animation completion */
    case PASS_SUB_TOSS_WAIT: return ANIM_PASS_WAIT_DURATION * anim_get_speed();
    case PASS_SUB_REVEAL:    return PASS_REVEAL_DURATION * anim_get_speed();
    case PASS_SUB_RECEIVE:   return 0.0f;  /* driven by animation completion */
    }
    return 0.0f;
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

void setup_vendetta_ui(RenderState *rs, Phase2State *p2, int timing_filter)
{
    int player = p2->round.vendetta_player_id;
    if (player < 0) return;

    int ids[MAX_VENDETTA_OPTIONS];
    int count = vendetta_get_available(p2, player, timing_filter, ids);

    const char *names[MAX_VENDETTA_OPTIONS];
    const char *descs[MAX_VENDETTA_OPTIONS];

    for (int i = 0; i < count; i++) {
        const VendettaDef *vd = phase2_get_vendetta(ids[i]);
        if (vd) {
            names[i] = vd->name;
            descs[i] = vd->description;
        } else {
            names[i] = "Unknown";
            descs[i] = "";
        }
    }

    render_set_contract_options(rs, ids, count, names, descs);
}

void advance_pass_subphase(PassPhaseState *pps, GameState *gs,
                           RenderState *rs, Phase2State *p2,
                           PassSubphase next)
{
    pps->subphase = next;
    pps->timer = 0.0f;
    rs->pass_subphase = next;
    rs->pass_subphase_remaining = pass_subphase_time_limit(next);
    rs->pass_status_text = NULL;

    switch (next) {
    case PASS_SUB_TOSS_ANIM:
    case PASS_SUB_TOSS_WAIT:
    case PASS_SUB_REVEAL:
    case PASS_SUB_RECEIVE:
        break;  /* animation subphases don't need setup here */
    case PASS_SUB_VENDETTA:
        if (p2->round.vendetta_player_id == 0) {
            pps->vendetta_ui_active = true;
            setup_vendetta_ui(rs, p2, VENDETTA_TIMING_PASSING);
            rs->pass_status_text = "Choose a " VENDETTA_DISPLAY_NAME ":";
        } else if (p2->round.vendetta_player_id > 0) {
            pps->ai_vendetta_pending = true;
            rs->pass_status_text = VENDETTA_DISPLAY_NAME " player is choosing...";
        }
        break;
    case PASS_SUB_CONTRACT:
        pps->vendetta_ui_active = false;
        pps->ai_vendetta_pending = false;
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
    assert(hand->count >= PASS_CARD_COUNT);
    Card pass_cards[PASS_CARD_COUNT];
    for (int i = 0; i < PASS_CARD_COUNT; i++) {
        pass_cards[i] = hand->cards[i];
    }
    game_state_select_pass(gs, 0, pass_cards);
    render_clear_selection(rs);
}

void finalize_card_pass(PassPhaseState *pps, GameState *gs,
                        RenderState *rs, Phase2State *p2)
{
    (void)pps;
    for (int p = 1; p < NUM_PLAYERS; p++) {
        if (!gs->pass_ready[p]) {
            ai_select_pass(gs, p);
        }
    }
    if (game_state_all_passes_ready(gs)) {
        typedef struct {
            int  tid;
            Card orig;
            Card transmuted;
            int  transmuter_player;
            bool fogged;
            int  fog_transmuter;
            bool is_passed;
        } SavedTransmute;
        SavedTransmute saved[NUM_PLAYERS][MAX_HAND_SIZE];
        int saved_count[NUM_PLAYERS] = {0};
        int pass_offset = 0;
        switch (gs->pass_direction) {
        case PASS_LEFT:   pass_offset = 1; break;
        case PASS_RIGHT:  pass_offset = 3; break;
        case PASS_ACROSS: pass_offset = 2; break;
        default:          pass_offset = 0; break;
        }

        if (p2->enabled) {
            for (int pl = 0; pl < NUM_PLAYERS; pl++) {
                Hand *hand = &gs->players[pl].hand;
                HandTransmuteState *hts = &p2->players[pl].hand_transmutes;
                for (int k = 0; k < hand->count; k++) {
                    if (!transmute_is_transmuted(hts, k)) continue;
                    int si = saved_count[pl]++;
                    saved[pl][si].tid = hts->slots[k].transmutation_id;
                    saved[pl][si].orig = hts->slots[k].original_card;
                    saved[pl][si].transmuted = hand->cards[k];
                    saved[pl][si].transmuter_player = hts->slots[k].transmuter_player;
                    saved[pl][si].fogged = hts->slots[k].fogged;
                    saved[pl][si].fog_transmuter = hts->slots[k].fog_transmuter;
                    saved[pl][si].is_passed = false;
                    for (int j = 0; j < PASS_CARD_COUNT; j++) {
                        if (card_equals(hand->cards[k],
                                        gs->pass_selections[pl][j])) {
                            saved[pl][si].is_passed = true;
                            break;
                        }
                    }
                }
            }
        }

        /* Record received cards for contract tracking */
        if (p2->enabled && gs->pass_direction != PASS_NONE) {
            for (int p = 0; p < NUM_PLAYERS; p++) {
                int dest = (p + pass_offset) % NUM_PLAYERS;
                contract_record_received_cards(p2, dest,
                                               gs->pass_selections[p],
                                               PASS_CARD_COUNT);
            }
        }

        gs->skip_human_pass_sort = false; /* legacy path: always sort */
        game_state_execute_pass(gs);

        if (p2->enabled) {
            for (int pl = 0; pl < NUM_PLAYERS; pl++) {
                transmute_hand_init(&p2->players[pl].hand_transmutes);
            }
            for (int pl = 0; pl < NUM_PLAYERS; pl++) {
                for (int si = 0; si < saved_count[pl]; si++) {
                    if (saved[pl][si].tid < 0) continue;
                    int owner;
                    if (saved[pl][si].is_passed) {
                        owner = (pl + pass_offset) % NUM_PLAYERS;
                    } else {
                        owner = pl;
                    }
                    Hand *ohand = &gs->players[owner].hand;
                    HandTransmuteState *ohts =
                        &p2->players[owner].hand_transmutes;
                    Card tc = saved[pl][si].transmuted;
                    for (int k = 0; k < ohand->count; k++) {
                        if (card_equals(ohand->cards[k], tc) &&
                            !transmute_is_transmuted(ohts, k)) {
                            ohts->slots[k].transmutation_id =
                                saved[pl][si].tid;
                            ohts->slots[k].original_card =
                                saved[pl][si].orig;
                            ohts->slots[k].transmuter_player =
                                saved[pl][si].transmuter_player;
                            ohts->slots[k].fogged =
                                saved[pl][si].fogged;
                            ohts->slots[k].fog_transmuter =
                                saved[pl][si].fog_transmuter;
                            break;
                        }
                    }
                }
            }
        }

        rs->sync_needed = true;
    }
}

/* ---- Pass animation helpers ---- */

static int pass_dest_player(int from, PassDirection dir)
{
    static const int offsets[PASS_COUNT] = {1, 3, 2, 0};
    return (from + offsets[dir]) % NUM_PLAYERS;
}

void pass_start_toss_anim(PassPhaseState *pps, GameState *gs,
                          RenderState *rs, Phase2State *p2)
{
    (void)p2;
    const LayoutConfig *cfg = &rs->layout;

    /* Ensure all AI players have selected their pass */
    for (int p = 1; p < NUM_PLAYERS; p++) {
        if (!gs->pass_ready[p]) {
            ai_select_pass(gs, p);
        }
    }

    rs->pass_staged_count = 0;
    rs->pass_anim_in_progress = true;

    for (int p = 0; p < NUM_PLAYERS; p++) {
        int dest = pass_dest_player(p, gs->pass_direction);
        PlayerPosition dest_spos = (PlayerPosition)dest;

        /* For human: save per-card start positions from selected cards.
         * For AI: compute a single start from hand midpoint. */
        Vector2 human_starts[PASS_CARD_COUNT];
        float   human_rots[PASS_CARD_COUNT];
        Vector2 ai_start = layout_board_center(cfg);
        float   ai_start_rot = 0.0f;

        if (p == 0) {
            /* Human: each card starts from its own hand position.
             * Selection was already cleared, so match pass_selections
             * against hand visuals to find positions. */
            for (int j = 0; j < PASS_CARD_COUNT; j++) {
                human_starts[j] = layout_board_center(cfg);
                human_rots[j] = 0.0f;
                for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
                    int vi = rs->hand_visuals[0][i];
                    if (vi >= 0 && vi < rs->card_count &&
                        card_equals(rs->cards[vi].card,
                                    gs->pass_selections[0][j])) {
                        human_starts[j] = rs->cards[vi].position;
                        human_rots[j] = rs->cards[vi].rotation;
                        break;
                    }
                }
            }
        } else {
            /* AI: use hand midpoint */
            if (rs->hand_visual_counts[p] > 0) {
                int mid = rs->hand_visual_counts[p] / 2;
                int mid_idx = rs->hand_visuals[p][mid];
                const CardVisual *mid_cv = &rs->cards[mid_idx];
                ai_start = (Vector2){
                    mid_cv->position.x - mid_cv->origin.x,
                    mid_cv->position.y - mid_cv->origin.y
                };
                ai_start_rot = mid_cv->rotation;
            }
        }

        /* Toss each of this player's 3 passed cards */
        for (int j = 0; j < PASS_CARD_COUNT; j++) {
            Vector2 start_pos = (p == 0) ? human_starts[j] : ai_start;
            float   start_rot = (p == 0) ? human_rots[j]   : ai_start_rot;

            Vector2 target = layout_pass_staging_position(
                dest_spos, j, PASS_CARD_COUNT, cfg);

            float delay = (float)p * PASS_PLAYER_STAGGER +
                          (float)j * PASS_TOSS_STAGGER;

            int idx = render_alloc_card_visual(rs);
            if (idx < 0) continue;

            CardVisual *cv = &rs->cards[idx];
            cv->card = gs->pass_selections[p][j];
            cv->face_up = false;
            cv->scale = 0.7f * cfg->scale;  /* AI-sized cards */
            cv->opacity = 1.0f;
            cv->z_order = 200 + rs->pass_staged_count;
            cv->origin = (Vector2){
                CARD_WIDTH_REF * cv->scale * 0.5f,
                CARD_HEIGHT_REF * cv->scale * 0.5f
            };

            anim_setup_toss(cv, start_pos, start_rot, target,
                            NULL, ANIM_PASS_TOSS_DURATION, delay);

            /* Record in staging array */
            PassStagedCard *sc = &rs->pass_staged[rs->pass_staged_count];
            sc->card_visual_idx = idx;
            sc->from_player = p;
            sc->to_player = dest;
            sc->card = gs->pass_selections[p][j];
            rs->pass_staged_count++;
        }
    }

    /* Remove tossed cards from hand visuals (visual only) */
    for (int p = 0; p < NUM_PLAYERS; p++) {
        for (int j = 0; j < PASS_CARD_COUNT; j++) {
            Card pass_card = gs->pass_selections[p][j];
            for (int i = 0; i < rs->hand_visual_counts[p]; i++) {
                int idx = rs->hand_visuals[p][i];
                if (card_equals(rs->cards[idx].card, pass_card)) {
                    rs->cards[idx].opacity = 0.0f;
                    break;
                }
            }
        }
    }

    /* Human hand: smoothly slide remaining cards to close gaps */
    {
        int remaining[MAX_HAND_SIZE];
        int remain_count = 0;
        for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
            int idx = rs->hand_visuals[0][i];
            if (rs->cards[idx].opacity > 0.0f) {
                remaining[remain_count++] = idx;
            }
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

    /* Advance to toss animation subphase */
    pps->subphase = PASS_SUB_TOSS_ANIM;
    pps->timer = 0.0f;
    rs->pass_subphase = PASS_SUB_TOSS_ANIM;
    rs->pass_subphase_remaining = 0.0f;
    rs->pass_status_text = NULL;
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

void pass_start_receive_anim(PassPhaseState *pps, GameState *gs,
                             RenderState *rs, Phase2State *p2,
                             const GameSettings *settings)
{
    /* Set sort flag based on setting */
    gs->skip_human_pass_sort = !settings->auto_sort_received;

    /* Save transmutation state before execute_pass modifies hands */
    typedef struct {
        int  tid;
        Card orig;
        Card transmuted;
        int  transmuter_player;
        bool fogged;
        int  fog_transmuter;
        bool is_passed;
    } SavedTransmute;
    SavedTransmute saved[NUM_PLAYERS][MAX_HAND_SIZE];
    int saved_count[NUM_PLAYERS] = {0};
    int pass_offset = 0;
    switch (gs->pass_direction) {
    case PASS_LEFT:   pass_offset = 1; break;
    case PASS_RIGHT:  pass_offset = 3; break;
    case PASS_ACROSS: pass_offset = 2; break;
    default:          pass_offset = 0; break;
    }

    if (p2->enabled) {
        for (int pl = 0; pl < NUM_PLAYERS; pl++) {
            Hand *hand = &gs->players[pl].hand;
            HandTransmuteState *hts = &p2->players[pl].hand_transmutes;
            for (int k = 0; k < hand->count; k++) {
                if (!transmute_is_transmuted(hts, k)) continue;
                int si = saved_count[pl]++;
                saved[pl][si].tid = hts->slots[k].transmutation_id;
                saved[pl][si].orig = hts->slots[k].original_card;
                saved[pl][si].transmuted = hand->cards[k];
                saved[pl][si].transmuter_player = hts->slots[k].transmuter_player;
                saved[pl][si].fogged = hts->slots[k].fogged;
                saved[pl][si].fog_transmuter = hts->slots[k].fog_transmuter;
                saved[pl][si].is_passed = false;
                for (int j = 0; j < PASS_CARD_COUNT; j++) {
                    if (card_equals(hand->cards[k],
                                    gs->pass_selections[pl][j])) {
                        saved[pl][si].is_passed = true;
                        break;
                    }
                }
            }
        }
    }

    /* Record received cards for contract tracking */
    if (p2->enabled && gs->pass_direction != PASS_NONE) {
        for (int p = 0; p < NUM_PLAYERS; p++) {
            int dest = (p + pass_offset) % NUM_PLAYERS;
            contract_record_received_cards(p2, dest,
                                           gs->pass_selections[p],
                                           PASS_CARD_COUNT);
        }
    }

    /* Execute logical card redistribution (no phase change now) */
    game_state_execute_pass(gs);

    /* Restore transmutation state */
    if (p2->enabled) {
        for (int pl = 0; pl < NUM_PLAYERS; pl++) {
            transmute_hand_init(&p2->players[pl].hand_transmutes);
        }
        for (int pl = 0; pl < NUM_PLAYERS; pl++) {
            for (int si = 0; si < saved_count[pl]; si++) {
                if (saved[pl][si].tid < 0) continue;
                int owner;
                if (saved[pl][si].is_passed) {
                    owner = (pl + pass_offset) % NUM_PLAYERS;
                } else {
                    owner = pl;
                }
                Hand *ohand = &gs->players[owner].hand;
                HandTransmuteState *ohts =
                    &p2->players[owner].hand_transmutes;
                Card tc = saved[pl][si].transmuted;
                for (int k = 0; k < ohand->count; k++) {
                    if (card_equals(ohand->cards[k], tc) &&
                        !transmute_is_transmuted(ohts, k)) {
                        ohts->slots[k].transmutation_id = saved[pl][si].tid;
                        ohts->slots[k].original_card = saved[pl][si].orig;
                        ohts->slots[k].transmuter_player =
                            saved[pl][si].transmuter_player;
                        ohts->slots[k].fogged = saved[pl][si].fogged;
                        ohts->slots[k].fog_transmuter =
                            saved[pl][si].fog_transmuter;
                        break;
                    }
                }
            }
        }
    }

    /* Keep pass_anim_in_progress = true during RECEIVE so sync doesn't
     * destroy staged visuals mid-flight. Cleared when RECEIVE completes. */

    const LayoutConfig *cfg = &rs->layout;

    /* ---- Human hand: slide existing cards to open gaps ---- */
    {
        const Hand *hhand = &gs->players[0].hand;
        int full_count = hhand->count; /* 13 after execute_pass */

        /* Identify which slots in the 13-card layout hold received cards */
        Card received[PASS_CARD_COUNT];
        int recv_count = 0;
        for (int i = 0; i < rs->pass_staged_count; i++) {
            if (rs->pass_staged[i].to_player == 0 && recv_count < PASS_CARD_COUNT)
                received[recv_count++] = rs->pass_staged[i].card;
        }

        bool is_gap[MAX_HAND_SIZE] = {false};
        if (settings->auto_sort_received) {
            /* Sorted hand: find indices of received cards.
             * Use 'used' flags to handle duplicate cards (transmutations). */
            bool used[PASS_CARD_COUNT] = {false};
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
            /* Unsorted: received cards are at the end */
            for (int k = full_count - recv_count; k < full_count; k++)
                is_gap[k] = true;
        }

        /* Compute 13-card layout positions */
        Vector2 positions13[MAX_HAND_SIZE];
        float rotations13[MAX_HAND_SIZE];
        int count13 = 0;
        layout_hand_positions(POS_BOTTOM, full_count, cfg,
                              positions13, rotations13, &count13);

        /* The 10 existing hand cards are still visible from the toss slide.
         * Find them and animate to their new 13-card-layout slots. */
        int existing_vis[MAX_HAND_SIZE];
        int existing_count = 0;
        for (int i = 0; i < rs->hand_visual_counts[0]; i++) {
            int idx = rs->hand_visuals[0][i];
            if (idx >= 0 && idx < rs->card_count &&
                rs->cards[idx].opacity > 0.0f) {
                existing_vis[existing_count++] = idx;
            }
        }

        /* Map existing visuals to non-gap slots */
        int vis_i = 0;
        for (int slot = 0; slot < count13 && vis_i < existing_count; slot++) {
            if (is_gap[slot]) continue;
            CardVisual *cv = &rs->cards[existing_vis[vis_i]];
            anim_start(cv, positions13[slot], rotations13[slot],
                       ANIM_PASS_HAND_SLIDE_DURATION, EASE_OUT_QUAD);
            vis_i++;
        }

        /* ---- Animate staged cards into gap slots ---- */
        int human_recv_idx = 0;
        /* Build ordered list of gap slot indices */
        int gap_slots[PASS_CARD_COUNT];
        int gap_count = 0;
        for (int k = 0; k < count13 && gap_count < PASS_CARD_COUNT; k++) {
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

    /* ---- AI staged cards: fly to hand center (no gap animation) ---- */
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

    /* Advance to receive subphase */
    pps->subphase = PASS_SUB_RECEIVE;
    pps->timer = 0.0f;
    rs->pass_subphase = PASS_SUB_RECEIVE;
    rs->pass_subphase_remaining = 0.0f;
    rs->pass_status_text = NULL;
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

/* ---- Reveal: animate human-destined cards to preview row ---- */

static void pass_start_reveal(PassPhaseState *pps, RenderState *rs)
{
    const LayoutConfig *cfg = &rs->layout;
    float human_scale = cfg->scale; /* full-size (rel_scale = 1.0) */

    /* Compute preview row positions for human-destined cards */
    Vector2 preview_pos[PASS_CARD_COUNT];
    layout_pass_preview_positions(PASS_CARD_COUNT, cfg, preview_pos);

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
    rs->pass_subphase_remaining = pass_subphase_time_limit(PASS_SUB_REVEAL);
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

static void handle_vendetta_subphase(PassPhaseState *pps, GameState *gs,
                                     RenderState *rs, Phase2State *p2,
                                     float dt)
{
    pps->timer += dt;

    if (pps->ai_vendetta_pending) {
        if (pps->timer >= PASS_AI_VENDETTA_DISPLAY) {
            vendetta_ai_activate(p2, VENDETTA_TIMING_PASSING);
            advance_pass_subphase(pps, gs, rs, p2, PASS_SUB_CONTRACT);
        }
    } else if (pps->vendetta_ui_active) {
        if (pps->timer >= pass_subphase_time_limit(PASS_SUB_VENDETTA)) {
            pps->vendetta_ui_active = false;
            advance_pass_subphase(pps, gs, rs, p2, PASS_SUB_CONTRACT);
        }
    } else {
        advance_pass_subphase(pps, gs, rs, p2, PASS_SUB_CONTRACT);
    }
}

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
        rs->pass_subphase_remaining = PASS_CONTRACT_TIME;
    }
}

static void handle_contract_subphase(PassPhaseState *pps, GameState *gs,
                                     RenderState *rs, Phase2State *p2,
                                     float dt)
{
    DraftState *draft = &p2->round.draft;
    if (!draft->active) return;

    pps->timer += dt;
    draft->timer -= dt;
    if (draft->timer < 0.0f) draft->timer = 0.0f;

    /* Timeout: auto-pick for human */
    if (pps->timer >= PASS_CONTRACT_TIME) {
        if (!draft->players[0].has_picked_this_round)
            draft_auto_pick(draft, 0);

        if (draft_all_picked(draft))
            draft_finish_round(pps, gs, rs, p2);
    }
}

static void handle_card_pass_subphase(PassPhaseState *pps, GameState *gs,
                                      RenderState *rs, Phase2State *p2,
                                      float dt)
{
    pps->timer += dt;
    float limit = pass_subphase_time_limit(PASS_SUB_CARD_PASS);

    if (pps->timer >= limit && !gs->pass_ready[0]) {
        auto_select_human_pass(gs, rs);
        pass_start_toss_anim(pps, gs, rs, p2);
    }
}

void pass_subphase_update(PassPhaseState *pps, GameState *gs,
                          RenderState *rs, Phase2State *p2,
                          const GameSettings *settings, float dt)
{
    if (gs->phase != PHASE_PASSING) return;

    float limit = pass_subphase_time_limit(pps->subphase);
    float remaining = limit - pps->timer;
    if (remaining < 0.0f) remaining = 0.0f;
    rs->pass_subphase_remaining = remaining;
    rs->pass_subphase = pps->subphase;

    switch (pps->subphase) {
    case PASS_SUB_VENDETTA:
        if (p2->enabled) handle_vendetta_subphase(pps, gs, rs, p2, dt);
        break;
    case PASS_SUB_CONTRACT:
        if (p2->enabled) handle_contract_subphase(pps, gs, rs, p2, dt);
        break;
    case PASS_SUB_CARD_PASS:
        handle_card_pass_subphase(pps, gs, rs, p2, dt);
        break;
    case PASS_SUB_TOSS_ANIM:
        if (pass_toss_animations_done(rs)) {
            pps->subphase = PASS_SUB_TOSS_WAIT;
            pps->timer = 0.0f;
            rs->pass_subphase = PASS_SUB_TOSS_WAIT;
            rs->pass_wait_timer = 0.0f;
        }
        break;
    case PASS_SUB_TOSS_WAIT:
        rs->pass_wait_timer += dt;
        if (rs->pass_wait_timer >= pass_subphase_time_limit(PASS_SUB_TOSS_WAIT)) {
            pass_start_reveal(pps, rs);
        }
        break;
    case PASS_SUB_REVEAL:
        /* Wait for fly-in animations to finish before starting hold timer */
        if (pass_reveal_animations_done(rs)) {
            pps->timer += dt;
            if (pps->timer >= pass_subphase_time_limit(PASS_SUB_REVEAL)) {
                pass_start_receive_anim(pps, gs, rs, p2, settings);
            }
        }
        break;
    case PASS_SUB_RECEIVE:
        if (pass_receive_animations_done(rs)) {
            rs->pass_staged_count = 0;
            rs->pass_anim_in_progress = false;
            rs->sync_needed = true;
            gs->phase = PHASE_PLAYING;
        }
        break;
    }
}
