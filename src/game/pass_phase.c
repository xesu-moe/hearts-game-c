/* ============================================================
 * @deps-implements: pass_phase.h
 * @deps-requires: pass_phase.h, core/game_state.h, core/hand.h, core/ai.h,
 *                 render/render.h, phase2/phase2_state.h,
 *                 phase2/contract_logic.h, phase2/vendetta_logic.h,
 *                 phase2/transmutation_logic.h, phase2/phase2_defs.h
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "pass_phase.h"

#include <assert.h>
#include <stdio.h>

#include "core/ai.h"
#include "core/hand.h"
#include "render/render.h"
#include "phase2/contract_logic.h"
#include "phase2/vendetta_logic.h"
#include "phase2/transmutation_logic.h"
#include "phase2/phase2_defs.h"

float pass_subphase_time_limit(PassSubphase sub)
{
    switch (sub) {
    case PASS_SUB_VENDETTA: return PASS_VENDETTA_TIME;
    case PASS_SUB_CONTRACT: return PASS_CONTRACT_TIME;
    case PASS_SUB_CARD_PASS: return PASS_CARD_PASS_TIME;
    }
    return 0.0f;
}

void setup_contract_ui(RenderState *rs, Phase2State *p2)
{
    int ids[MAX_CONTRACT_OPTIONS];
    int count = contract_get_available(p2, 0, ids);

    const char *names[MAX_CONTRACT_OPTIONS];
    const char *descs[MAX_CONTRACT_OPTIONS];

    for (int i = 0; i < count; i++) {
        const ContractDef *cd = phase2_get_contract(ids[i]);
        if (cd) {
            names[i] = cd->name;
            descs[i] = cd->description;
        } else {
            names[i] = "Unknown";
            descs[i] = "";
        }
    }

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
    (void)gs;
    pps->subphase = next;
    pps->timer = 0.0f;
    rs->pass_subphase = next;
    rs->pass_subphase_remaining = pass_subphase_time_limit(next);
    rs->pass_status_text = NULL;

    switch (next) {
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
        setup_contract_ui(rs, p2);
        rs->pass_status_text = "Choose a Contract:";
        break;
    case PASS_SUB_CARD_PASS:
        rs->contract_ui_active = false;
        rs->pass_status_text = NULL;
        /* AI players apply transmutations at card pass start */
        if (p2->enabled) {
            for (int p = 1; p < NUM_PLAYERS; p++) {
                transmute_ai_apply(&gs->players[p].hand,
                                   &p2->players[p].hand_transmutes,
                                   &p2->players[p].transmute_inv, true);
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
                            break;
                        }
                    }
                }
            }
        }

        rs->sync_needed = true;
    }
}

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

static void handle_contract_subphase(PassPhaseState *pps, GameState *gs,
                                     RenderState *rs, Phase2State *p2,
                                     float dt)
{
    pps->timer += dt;
    float limit = pass_subphase_time_limit(PASS_SUB_CONTRACT);

    if (pps->timer >= limit) {
        if (p2->players[0].contract.contract_id < 0 &&
            rs->contract_option_count > 0) {
            contract_select(p2, 0, rs->contract_option_ids[0]);
            rs->selected_contract_idx = 0;
        }
        for (int p = 1; p < NUM_PLAYERS; p++) {
            if (p2->players[p].contract.contract_id < 0) {
                contract_ai_select(p2, p);
            }
        }
        p2->round.contracts_chosen = contract_all_chosen(p2);
        rs->contract_ui_active = false;

        if (gs->pass_direction == PASS_NONE) {
            gs->phase = PHASE_PLAYING;
            rs->sync_needed = true;
        } else {
            advance_pass_subphase(pps, gs, rs, p2, PASS_SUB_CARD_PASS);
        }
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
        finalize_card_pass(pps, gs, rs, p2);
    }
}

void pass_subphase_update(PassPhaseState *pps, GameState *gs,
                          RenderState *rs, Phase2State *p2, float dt)
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
    }
}
