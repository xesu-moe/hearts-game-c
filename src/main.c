/* ============================================================
 * @deps-implements: (entry point)
 * @deps-requires: core/game_state.h (GameState, game_state_init),
 *                 core/input.h (InputCmd, input_init, input_poll),
 *                 render/render.h (RenderState, render_init, render_draw),
 *                 render/card_render.h (card_render_init, card_render_shutdown),
 *                 render/particle.h (particle_spawn_burst),
 *                 phase2/phase2_defs.h (phase2_defs_init),
 *                 phase2/contract_logic.h (contract_state_init),
 *                 phase2/host_action_logic.h (host_action_*),
 *                 phase2/grudge_logic.h (grudge_state_init,
 *                     grudge_check_trick, grudge_grant_token,
 *                     grudge_ai_decide, grudge_apply_revenge),
 *                 phase2/revenge.h (RevengeDef), raylib.h
 * @deps-last-changed: 2026-03-17 — Added particle_spawn_burst call for hearts broken effect
 * ============================================================ */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "raylib.h"

#include "render/card_render.h"
#include "core/game_state.h"
#include "core/input.h"
#include "core/settings.h"
#include "render/render.h"
#include "phase2/phase2_defs.h"
#include "phase2/contract_logic.h"
#include "phase2/host_action_logic.h"
#include "phase2/grudge_logic.h"
#include "phase2/revenge.h"

/* ---- Constants ---- */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720
#define WINDOW_TITLE  "Hollow Hearts"
#define TARGET_FPS    60
#define FIXED_DT        (1.0f / 60.0f)
#define MAX_FRAME_DT    0.25f
#define MAX_CATCHUP     5

/* ---- Game clock ---- */
typedef struct GameClock {
    float raw_dt;
    float dt;
    float accumulator;
    float total_time;
    float time_scale;
    bool  paused;
} GameClock;

static void clock_init(GameClock *clk)
{
    clk->raw_dt      = 0.0f;
    clk->dt          = 0.0f;
    clk->accumulator = 0.0f;
    clk->total_time  = 0.0f;
    clk->time_scale  = 1.0f;
    clk->paused      = false;
}

static void clock_update(GameClock *clk)
{
    clk->raw_dt = GetFrameTime();
    if (clk->raw_dt > MAX_FRAME_DT) {
        clk->raw_dt = MAX_FRAME_DT;
    }

    if (clk->paused) {
        clk->dt = 0.0f;
    } else {
        clk->dt = clk->raw_dt * clk->time_scale;
        clk->accumulator += clk->dt;
        clk->total_time  += clk->dt;
        float max_acc = FIXED_DT * MAX_CATCHUP;
        if (clk->accumulator > max_acc) {
            clk->accumulator = max_acc;
        }
    }
}

/* ---- Turn flow pacing ---- */

typedef enum FlowStep {
    FLOW_IDLE,              /* Decide what to do next */
    FLOW_WAITING_FOR_HUMAN, /* Human's turn — wait for input */
    FLOW_AI_THINKING,       /* Delay before AI plays (0.4s) */
    FLOW_CARD_ANIMATING,    /* Card sliding from hand to trick (0.25s) */
    FLOW_TRICK_DISPLAY,     /* Show complete trick (1.0s) */
    FLOW_TRICK_COLLECTING,  /* Cards slide to winner (0.3s) */
    FLOW_BETWEEN_TRICKS,    /* Pause before next trick (0.2s) */
    FLOW_GRUDGE_DISCARD,    /* Human choosing old vs new token */
} FlowStep;

typedef struct TurnFlow {
    FlowStep step;
    float    timer;
    int      animating_player;  /* whose card is animating (-1 = none) */
    int      prev_trick_count;  /* for detecting human play */
} TurnFlow;

#define FLOW_AI_THINK_TIME     0.4f
#define FLOW_CARD_ANIM_TIME    0.25f
#define FLOW_TRICK_DISPLAY_TIME 1.0f
#define FLOW_TRICK_COLLECT_TIME 0.3f
#define FLOW_BETWEEN_TRICKS_TIME 0.2f

/* Pass subphase timers */
#define PASS_HOST_ACTION_TIME    10.0f
#define PASS_CONTRACT_TIME       16.0f
#define PASS_CARD_PASS_TIME      16.0f
#define PASS_AI_HOST_DISPLAY     1.2f  /* brief "AI is choosing..." pause */

static void flow_init(TurnFlow *flow)
{
    flow->step = FLOW_IDLE;
    flow->timer = 0.0f;
    flow->animating_player = -1;
    flow->prev_trick_count = 0;
}

static void flow_update(TurnFlow *flow, GameState *gs, RenderState *rs,
                         float dt);

static const char *p2_player_name(int player_id)
{
    static const char *names[] = {"You", "West", "North", "East"};
    if (player_id >= 0 && player_id < NUM_PLAYERS) return names[player_id];
    return "???";
}

/* ---- Settings state (file-level so all functions can access it) ---- */

static GameSettings g_settings;

/* ---- Phase2 state (file-level so all functions can access it) ---- */

static Phase2State p2;
static bool host_action_ui_active = false;

/* Pass subphase state */
static PassSubphase pass_subphase       = PASS_SUB_HOST_ACTION;
static float        pass_subphase_timer = 0.0f;
static bool         pass_sub_ai_host    = false;  /* true when AI host auto-selecting */

/* Populate contract UI for human player from Phase2State */
static void setup_contract_ui(RenderState *rs)
{
    int ids[MAX_CONTRACT_OPTIONS];
    int count = contract_get_available(&p2, 0, ids);

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

/* Populate host action UI for host player using contract option rendering */
static void setup_host_action_ui(RenderState *rs)
{
    int host = p2.round.host_player_id;
    if (host < 0) return;

    int ids[MAX_HOST_ACTION_OPTIONS];
    int count = host_action_get_available(&p2, host, ids);

    const char *names[MAX_HOST_ACTION_OPTIONS];
    const char *descs[MAX_HOST_ACTION_OPTIONS];

    for (int i = 0; i < count; i++) {
        const HostActionDef *ha = phase2_get_host_action(ids[i]);
        if (ha) {
            names[i] = ha->name;
            descs[i] = ha->description;
        } else {
            names[i] = "Unknown";
            descs[i] = "";
        }
    }

    render_set_contract_options(rs, ids, count, names, descs);
}

/* Forward declarations for AI stubs (used by subphase helpers) */
static void ai_select_pass(GameState *gs, int player_id);

/* ---- Pass subphase helpers ---- */

static float subphase_time_limit(PassSubphase sub)
{
    switch (sub) {
    case PASS_SUB_HOST_ACTION: return PASS_HOST_ACTION_TIME;
    case PASS_SUB_CONTRACT:    return PASS_CONTRACT_TIME;
    case PASS_SUB_CARD_PASS:   return PASS_CARD_PASS_TIME;
    }
    return 0.0f;
}

static void advance_pass_subphase(GameState *gs, RenderState *rs,
                                   PassSubphase next)
{
    (void)gs;
    pass_subphase = next;
    pass_subphase_timer = 0.0f;
    rs->pass_subphase = next;
    rs->pass_subphase_remaining = subphase_time_limit(next);
    rs->pass_status_text = NULL;

    switch (next) {
    case PASS_SUB_HOST_ACTION:
        /* Set up host action UI */
        if (p2.round.host_player_id == 0) {
            host_action_ui_active = true;
            setup_host_action_ui(rs);
            rs->pass_status_text = "Choose a round modifier:";
        } else if (p2.round.host_player_id > 0) {
            pass_sub_ai_host = true;
            rs->pass_status_text = "Host is choosing the round modifier...";
        }
        break;
    case PASS_SUB_CONTRACT:
        host_action_ui_active = false;
        pass_sub_ai_host = false;
        setup_contract_ui(rs);
        rs->pass_status_text = "Choose a Contract:";
        break;
    case PASS_SUB_CARD_PASS:
        rs->contract_ui_active = false;
        rs->pass_status_text = NULL;
        break;
    }
}

/* Auto-select first 3 cards from hand for timeout.
 * Asserts that hand has enough cards — at pass time hands always have 13. */
static void auto_select_human_pass(GameState *gs, RenderState *rs)
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

/* AI selects passes + execute pass, transition to PLAYING */
static void finalize_card_pass(GameState *gs, RenderState *rs)
{
    for (int p = 1; p < NUM_PLAYERS; p++) {
        if (!gs->pass_ready[p]) {
            ai_select_pass(gs, p);
        }
    }
    if (game_state_all_passes_ready(gs)) {
        game_state_execute_pass(gs);
        rs->sync_needed = true;
    }
}

static void handle_host_action_subphase(GameState *gs, RenderState *rs,
                                         float dt)
{
    pass_subphase_timer += dt;

    if (pass_sub_ai_host) {
        /* AI host: show message briefly, then auto-select */
        if (pass_subphase_timer >= PASS_AI_HOST_DISPLAY) {
            host_action_ai_select(&p2);
            host_action_apply(&p2);
            advance_pass_subphase(gs, rs, PASS_SUB_CONTRACT);
        }
    } else if (host_action_ui_active) {
        /* Human host: wait for selection or timeout (skip) */
        if (pass_subphase_timer >= subphase_time_limit(PASS_SUB_HOST_ACTION)) {
            host_action_ui_active = false;
            advance_pass_subphase(gs, rs, PASS_SUB_CONTRACT);
        }
    } else {
        /* No host or unexpected state — advance immediately */
        advance_pass_subphase(gs, rs, PASS_SUB_CONTRACT);
    }
}

static void handle_contract_subphase(GameState *gs, RenderState *rs,
                                      float dt)
{
    pass_subphase_timer += dt;
    float limit = subphase_time_limit(PASS_SUB_CONTRACT);

    if (pass_subphase_timer >= limit) {
        /* Timeout: auto-select first contract for human */
        if (p2.players[0].contract.contract_id < 0 &&
            rs->contract_option_count > 0) {
            contract_select(&p2, 0, rs->contract_option_ids[0]);
            rs->selected_contract_idx = 0;
        }
        /* AI selects contracts */
        for (int p = 1; p < NUM_PLAYERS; p++) {
            if (p2.players[p].contract.contract_id < 0) {
                contract_ai_select(&p2, p);
            }
        }
        p2.round.contracts_chosen = contract_all_chosen(&p2);
        rs->contract_ui_active = false;

        if (gs->pass_direction == PASS_NONE) {
            /* No card passing — go directly to PLAYING */
            gs->phase = PHASE_PLAYING;
            rs->sync_needed = true;
        } else {
            advance_pass_subphase(gs, rs, PASS_SUB_CARD_PASS);
        }
    }
}

static void handle_card_pass_subphase(GameState *gs, RenderState *rs,
                                       float dt)
{
    pass_subphase_timer += dt;
    float limit = subphase_time_limit(PASS_SUB_CARD_PASS);

    if (pass_subphase_timer >= limit && !gs->pass_ready[0]) {
        /* Timeout: auto-select cards for human */
        auto_select_human_pass(gs, rs);
        finalize_card_pass(gs, rs);
    }
}

static void pass_subphase_update(GameState *gs, RenderState *rs, float dt)
{
    if (gs->phase != PHASE_PASSING) return;

    /* Update countdown display */
    float limit = subphase_time_limit(pass_subphase);
    float remaining = limit - pass_subphase_timer;
    if (remaining < 0.0f) remaining = 0.0f;
    rs->pass_subphase_remaining = remaining;
    rs->pass_subphase = pass_subphase;

    switch (pass_subphase) {
    case PASS_SUB_HOST_ACTION:
        if (p2.enabled) handle_host_action_subphase(gs, rs, dt);
        break;
    case PASS_SUB_CONTRACT:
        if (p2.enabled) handle_contract_subphase(gs, rs, dt);
        break;
    case PASS_SUB_CARD_PASS:
        handle_card_pass_subphase(gs, rs, dt);
        break;
    }
}

/* ---- AI stubs ---- */

/* AI passes the 3 highest-point cards. Ties broken by suit/rank order. */
static void ai_select_pass(GameState *gs, int player_id)
{
    Hand *hand = &gs->players[player_id].hand;
    Card pass_cards[PASS_CARD_COUNT];
    int pass_count = 0;

    /* Simple heuristic: pick cards with highest point value, then highest rank */
    bool used[MAX_HAND_SIZE] = {false};

    for (int p = 0; p < PASS_CARD_COUNT; p++) {
        int best = -1;
        int best_points = -1;
        int best_rank = -1;
        for (int i = 0; i < hand->count; i++) {
            if (used[i]) continue;
            int pts = card_points(hand->cards[i]);
            int rnk = hand->cards[i].rank;
            if (pts > best_points || (pts == best_points && rnk > best_rank)) {
                best = i;
                best_points = pts;
                best_rank = rnk;
            }
        }
        if (best >= 0) {
            pass_cards[pass_count++] = hand->cards[best];
            used[best] = true;
        }
    }

    if (pass_count == PASS_CARD_COUNT) {
        game_state_select_pass(gs, player_id, pass_cards);
    }
}

/* AI plays the first legal card from hand. */
static void ai_play_card(GameState *gs, int player_id)
{
    const Hand *hand = &gs->players[player_id].hand;
    for (int i = 0; i < hand->count; i++) {
        if (game_state_is_valid_play(gs, player_id, hand->cards[i])) {
            game_state_play_card(gs, player_id, hand->cards[i]);
            return;
        }
    }
}

/* ---- Turn flow state machine ---- */

static void flow_update(TurnFlow *flow, GameState *gs, RenderState *rs,
                         float dt)
{
    /* Only active during PHASE_PLAYING */
    if (gs->phase != PHASE_PLAYING) {
        flow->step = FLOW_IDLE;
        return;
    }

    /* Tick timer */
    flow->timer -= dt;

    switch (flow->step) {
    case FLOW_IDLE: {
        float anim_mult = settings_anim_multiplier(g_settings.anim_speed);
        /* Check if trick is complete */
        if (trick_is_complete(&gs->current_trick)) {
            flow->step = FLOW_TRICK_DISPLAY;
            flow->timer = FLOW_TRICK_DISPLAY_TIME * anim_mult;
            rs->last_trick_winner = -1; /* will be set on collection */
            return;
        }

        /* Whose turn is it? */
        {
            int current = game_state_current_player(gs);
            if (current == 0) {
                /* Human's turn */
                flow->step = FLOW_WAITING_FOR_HUMAN;
                flow->prev_trick_count = gs->current_trick.num_played;
            } else if (current > 0) {
                /* AI's turn */
                flow->step = FLOW_AI_THINKING;
                flow->timer = settings_ai_think_time(g_settings.ai_speed);
            }
            /* current == -1 means no valid player; stay IDLE and
               let the phase-change guard at top handle it next frame */
        }
        break;
    }

    case FLOW_WAITING_FOR_HUMAN: {
        float anim_mult = settings_anim_multiplier(g_settings.anim_speed);
        /* Detect if human played a card (num_played increased) */
        if (gs->current_trick.num_played > flow->prev_trick_count) {
            rs->sync_needed = true;
            rs->anim_play_player = 0;
            flow->step = FLOW_CARD_ANIMATING;
            flow->timer = FLOW_CARD_ANIM_TIME * anim_mult;
        }
        break;
    }

    case FLOW_AI_THINKING: {
        float anim_mult = settings_anim_multiplier(g_settings.anim_speed);
        if (flow->timer <= 0.0f) {
            int current = game_state_current_player(gs);
            if (current > 0) {
                if (p2.enabled) grudge_ai_decide(&p2, current);
                ai_play_card(gs, current);
                rs->sync_needed = true;
                rs->anim_play_player = current;
                flow->step = FLOW_CARD_ANIMATING;
                flow->timer = FLOW_CARD_ANIM_TIME * anim_mult;
            } else {
                /* Turn changed unexpectedly; re-evaluate */
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
        float anim_mult = settings_anim_multiplier(g_settings.anim_speed);
        if (flow->timer <= 0.0f) {
            flow->step = FLOW_TRICK_COLLECTING;
            flow->timer = FLOW_TRICK_COLLECT_TIME * anim_mult;
        }
        break;
    }

    case FLOW_TRICK_COLLECTING:
        if (flow->timer <= 0.0f) {
            /* Save trick data before completion reinitializes it */
            if (p2.enabled && trick_is_complete(&gs->current_trick)) {
                Trick saved_trick = gs->current_trick;
                int winner = trick_get_winner(&saved_trick);
                game_state_complete_trick(gs);
                if (winner >= 0) {
                    /* Chat: trick winner */
                    {
                        char msg[CHAT_MSG_LEN];
                        snprintf(msg, sizeof(msg), "%s took trick %d",
                                 p2_player_name(winner),
                                 gs->tricks_played);
                        render_chat_log_push(rs, msg);
                    }

                    contract_on_trick_complete(&p2, &saved_trick, winner);

                    /* QoS grudge detection */
                    int g_attacker, g_victim;
                    if (grudge_check_trick(&saved_trick, winner,
                                           &g_attacker, &g_victim)) {
                        /* Chat: QoS hit */
                        {
                            char msg[CHAT_MSG_LEN];
                            snprintf(msg, sizeof(msg), "QoS! %s hit %s",
                                     p2_player_name(g_attacker),
                                     p2_player_name(g_victim));
                            render_chat_log_push(rs, msg);
                        }
                        int result = grudge_grant_token(&p2, g_victim,
                                                        g_attacker);
                        if (result == 1) {
                            if (g_victim == 0) {
                                /* Human: show discard choice */
                                rs->grudge_discard_ui = true;
                                rs->grudge_pending_attacker = g_attacker;
                                rs->sync_needed = true;
                                flow->step = FLOW_GRUDGE_DISCARD;
                                break;
                            } else {
                                /* AI: keep newer token */
                                grudge_set_token(&p2, g_victim, g_attacker);
                            }
                        }
                    }
                }
            } else {
                int winner = trick_get_winner(&gs->current_trick);
                game_state_complete_trick(gs);
                /* Chat: trick winner (vanilla) */
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
                          settings_anim_multiplier(g_settings.anim_speed);
        }
        break;

    case FLOW_BETWEEN_TRICKS:
        if (flow->timer <= 0.0f) {
            flow->step = FLOW_IDLE;
        }
        break;

    case FLOW_GRUDGE_DISCARD:
        if (!rs->grudge_discard_ui) {
            /* Choice made — continue to between tricks */
            rs->sync_needed = true;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = FLOW_BETWEEN_TRICKS_TIME *
                          settings_anim_multiplier(g_settings.anim_speed);
        }
        break;
    }
}

/* ---- Input processing with hit-testing ---- */

static void process_input(GameState *gs, RenderState *rs, FlowStep flow_step)
{
    input_poll();

    const InputState *is = input_get_state();

    /* Generate game-layer commands from mouse clicks */
    if (is->pressed[INPUT_ACTION_LEFT_CLICK]) {
        Vector2 mouse = is->mouse_pos;

        switch (gs->phase) {
        case PHASE_MENU:
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                if (render_hit_test_button(&rs->menu_items[i], mouse)) {
                    switch ((MenuItem)i) {
                    case MENU_PLAY_OFFLINE:
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_START_GAME,
                            .source_player = 0,
                        });
                        break;
                    case MENU_SETTINGS:
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_OPEN_SETTINGS,
                            .source_player = 0,
                        });
                        break;
                    case MENU_EXIT:
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_QUIT,
                            .source_player = 0,
                        });
                        break;
                    default:
                        break;
                    }
                    break;
                }
            }
            break;

        case PHASE_PASSING: {
            if (pass_subphase == PASS_SUB_HOST_ACTION ||
                pass_subphase == PASS_SUB_CONTRACT) {
                /* Only contract/host action buttons */
                int contract_hit = render_hit_test_contract(rs, mouse);
                if (contract_hit >= 0) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SELECT_CONTRACT,
                        .source_player = 0,
                        .contract = { .contract_id = rs->contract_option_ids[contract_hit] },
                    });
                }
            } else if (pass_subphase == PASS_SUB_CARD_PASS) {
                /* Confirm button and card clicks only */
                if (render_hit_test_button(&rs->btn_confirm_pass, mouse)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_CONFIRM,
                        .source_player = 0,
                    });
                    break;
                }
                int hit = render_hit_test_card(rs, mouse);
                if (hit >= 0) {
                    render_toggle_card_selection(rs, hit);
                }
            }
            break;
        }

        case PHASE_PLAYING: {
            /* Revenge panel buttons (non-blocking, during card play) */
            if (flow_step == FLOW_WAITING_FOR_HUMAN &&
                rs->info_grudge_available && rs->info_grudge_interactive) {
                bool revenge_hit = false;
                for (int i = 0; i < rs->info_revenge_count; i++) {
                    if (render_hit_test_button(&rs->info_revenge_btns[i],
                                               mouse)) {
                        input_cmd_push((InputCmd){
                            .type = INPUT_CMD_SELECT_GRUDGE_REVENGE,
                            .source_player = 0,
                            .grudge_revenge = {
                                .revenge_id = rs->info_revenge_ids[i]},
                        });
                        revenge_hit = true;
                        break;
                    }
                }
                if (!revenge_hit &&
                    render_hit_test_button(&rs->info_revenge_skip_btn,
                                           mouse)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SKIP_GRUDGE,
                        .source_player = 0,
                    });
                    revenge_hit = true;
                }
                if (revenge_hit) break;
            }
            /* Grudge discard UI */
            if (flow_step == FLOW_GRUDGE_DISCARD) {
                if (render_hit_test_button(&rs->btn_keep_old_grudge, mouse)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_GRUDGE_DISCARD_CHOICE,
                        .source_player = 0,
                        .grudge_discard = {.keep_new = 0},
                    });
                } else if (render_hit_test_button(&rs->btn_keep_new_grudge,
                                                   mouse)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_GRUDGE_DISCARD_CHOICE,
                        .source_player = 0,
                        .grudge_discard = {.keep_new = 1},
                    });
                }
                break;
            }
            /* Normal card play */
            int current = game_state_current_player(gs);
            if (current == 0 && flow_step == FLOW_WAITING_FOR_HUMAN) {
                int hit = render_hit_test_card(rs, mouse);
                if (hit >= 0) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_PLAY_CARD,
                        .source_player = 0,
                        .card = {
                            .card_index = -1,
                            .card = rs->cards[hit].card,
                        },
                    });
                }
            }
            break;
        }

        case PHASE_SETTINGS:
            /* Back button */
            if (render_hit_test_button(&rs->btn_settings_back, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_CANCEL,
                    .source_player = 0,
                });
                break;
            }
            /* Apply display settings button */
            if (render_hit_test_button(&rs->btn_settings_apply, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_APPLY_DISPLAY,
                    .source_player = 0,
                });
                break;
            }
            /* Arrow buttons */
            for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
                if (render_hit_test_button(&rs->settings_rows_prev[i], mouse)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SETTING_PREV,
                        .source_player = 0,
                        .setting = {.setting_id = i},
                    });
                    break;
                }
                if (render_hit_test_button(&rs->settings_rows_next[i], mouse)) {
                    input_cmd_push((InputCmd){
                        .type = INPUT_CMD_SETTING_NEXT,
                        .source_player = 0,
                        .setting = {.setting_id = i},
                    });
                    break;
                }
            }
            break;

        case PHASE_SCORING:
        case PHASE_GAME_OVER:
            if (render_hit_test_button(&rs->btn_continue, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_CONFIRM,
                    .source_player = 0,
                });
            }
            break;

        default:
            break;
        }
    }
}

/* ---- Settings value sync ---- */

/* Tracks pending display settings that haven't been applied yet */
static GameSettings g_pending_display;
static bool g_display_pending = false;

static void sync_settings_values(RenderState *rs)
{
    /* Show pending display values if unapplied, otherwise show active */
    WindowMode show_wm = g_display_pending ? g_pending_display.window_mode
                                           : g_settings.window_mode;
    int show_res = g_display_pending ? g_pending_display.resolution_index
                                     : g_settings.resolution_index;
    int show_fps = g_display_pending ? g_pending_display.fps_index
                                     : g_settings.fps_index;

    /* Copy into owned buffers to avoid stale static-buffer pointers */
    #define COPY_SETTING(i, str) snprintf(rs->settings_value_bufs[i], \
        sizeof(rs->settings_value_bufs[i]), "%s", (str))
    COPY_SETTING(0, settings_window_mode_name(show_wm));
    COPY_SETTING(1, settings_resolution_name(show_res));
    COPY_SETTING(2, settings_fps_name(show_fps));
    COPY_SETTING(3, settings_anim_speed_name(g_settings.anim_speed));
    COPY_SETTING(4, settings_ai_speed_name(g_settings.ai_speed));
    COPY_SETTING(5, "(No Audio)");
    COPY_SETTING(6, "(No Audio)");
    COPY_SETTING(7, "(No Audio)");
    #undef COPY_SETTING
}

/* ---- Settings change helpers ---- */

static void setting_adjust(int setting_id, int delta, RenderState *rs)
{
    switch (setting_id) {
    case 0: /* window_mode — deferred until Apply */
        if (!g_display_pending) {
            g_pending_display.window_mode = g_settings.window_mode;
            g_pending_display.resolution_index = g_settings.resolution_index;
            g_pending_display.fps_index = g_settings.fps_index;
        }
        g_pending_display.window_mode = (WindowMode)(
            ((int)g_pending_display.window_mode + delta +
             WINDOW_MODE_COUNT) % WINDOW_MODE_COUNT);
        g_display_pending = true;
        sync_settings_values(rs);
        return;
    case 1: /* resolution — deferred until Apply */
        if (!g_display_pending) {
            g_pending_display.window_mode = g_settings.window_mode;
            g_pending_display.resolution_index = g_settings.resolution_index;
            g_pending_display.fps_index = g_settings.fps_index;
        }
        g_pending_display.resolution_index =
            (g_pending_display.resolution_index + delta +
             RESOLUTION_COUNT) % RESOLUTION_COUNT;
        g_display_pending = true;
        sync_settings_values(rs);
        return;
    case 2: /* fps — deferred until Apply */
        if (!g_display_pending) {
            g_pending_display.window_mode = g_settings.window_mode;
            g_pending_display.resolution_index = g_settings.resolution_index;
            g_pending_display.fps_index = g_settings.fps_index;
        }
        g_pending_display.fps_index =
            (g_pending_display.fps_index + delta +
             FPS_OPTION_COUNT) % FPS_OPTION_COUNT;
        g_display_pending = true;
        sync_settings_values(rs);
        return;
    case 3: /* anim_speed — immediate */
        g_settings.anim_speed = (AnimSpeed)(((int)g_settings.anim_speed + delta +
                                              ANIM_SPEED_COUNT) % ANIM_SPEED_COUNT);
        break;
    case 4: /* ai_speed — immediate */
        g_settings.ai_speed = (AISpeed)(((int)g_settings.ai_speed + delta +
                                          AI_SPEED_COUNT) % AI_SPEED_COUNT);
        break;
    default:
        return; /* audio rows — skip */
    }

    g_settings.dirty = true;
    sync_settings_values(rs);
}

static void apply_display_settings(RenderState *rs)
{
    if (!g_display_pending) return;
    g_settings.window_mode = g_pending_display.window_mode;
    g_settings.resolution_index = g_pending_display.resolution_index;
    g_settings.fps_index = g_pending_display.fps_index;
    g_settings.dirty = true;
    g_display_pending = false;
    settings_apply(&g_settings, &rs->layout);
    sync_settings_values(rs);
}

/* ---- Game update ---- */

static void update(GameState *gs, RenderState *rs, float dt,
                   bool *quit_requested)
{
    (void)dt;

    InputCmd cmd;
    for (int n = 0; n < INPUT_CMD_QUEUE_CAPACITY &&
                    (cmd = input_cmd_pop()).type != INPUT_CMD_NONE; n++) {
        switch (gs->phase) {
        case PHASE_MENU:
            if (cmd.type == INPUT_CMD_START_GAME ||
                cmd.type == INPUT_CMD_CONFIRM) {
                game_state_start_game(gs);
                rs->sync_needed = true;
            } else if (cmd.type == INPUT_CMD_OPEN_SETTINGS) {
                gs->phase = PHASE_SETTINGS;
                rs->sync_needed = true;
                sync_settings_values(rs);
            } else if (cmd.type == INPUT_CMD_QUIT ||
                       cmd.type == INPUT_CMD_CANCEL) {
                *quit_requested = true;
            }
            break;

        case PHASE_SETTINGS:
            if (cmd.type == INPUT_CMD_CANCEL) {
                /* Discard unapplied display changes */
                g_display_pending = false;
                if (g_settings.dirty) {
                    settings_save(&g_settings);
                    g_settings.dirty = false;
                }
                gs->phase = PHASE_MENU;
                rs->sync_needed = true;
            } else if (cmd.type == INPUT_CMD_APPLY_DISPLAY) {
                apply_display_settings(rs);
            } else if (cmd.type == INPUT_CMD_SETTING_PREV) {
                setting_adjust(cmd.setting.setting_id, -1, rs);
            } else if (cmd.type == INPUT_CMD_SETTING_NEXT) {
                setting_adjust(cmd.setting.setting_id, 1, rs);
            }
            break;

        case PHASE_DEALING:
            break;

        case PHASE_PASSING:
            if (cmd.type == INPUT_CMD_SELECT_CONTRACT && p2.enabled) {
                int cid = cmd.contract.contract_id;
                if (pass_subphase == PASS_SUB_HOST_ACTION &&
                    host_action_ui_active) {
                    /* Host action selection */
                    host_action_select(&p2, cid);
                    host_action_apply(&p2);
                    host_action_ui_active = false;
                    advance_pass_subphase(gs, rs, PASS_SUB_CONTRACT);
                } else if (pass_subphase == PASS_SUB_CONTRACT) {
                    /* Contract selection — selecting confirms */
                    for (int i = 0; i < rs->contract_option_count; i++) {
                        if (rs->contract_option_ids[i] == cid) {
                            rs->selected_contract_idx = i;
                            contract_select(&p2, 0, cid);

                            /* AI selects contracts */
                            for (int p = 1; p < NUM_PLAYERS; p++) {
                                if (p2.players[p].contract.contract_id < 0) {
                                    contract_ai_select(&p2, p);
                                }
                            }
                            p2.round.contracts_chosen =
                                contract_all_chosen(&p2);
                            rs->contract_ui_active = false;

                            if (gs->pass_direction == PASS_NONE) {
                                gs->phase = PHASE_PLAYING;
                                rs->sync_needed = true;
                            } else {
                                advance_pass_subphase(gs, rs,
                                                       PASS_SUB_CARD_PASS);
                            }
                            break;
                        }
                    }
                }
            }
            if (cmd.type == INPUT_CMD_CONFIRM &&
                pass_subphase == PASS_SUB_CARD_PASS &&
                rs->selected_count == PASS_CARD_COUNT) {
                /* Gather selected cards from render state */
                Card pass_cards[PASS_CARD_COUNT];
                bool valid = true;
                for (int i = 0; i < PASS_CARD_COUNT; i++) {
                    int idx = rs->selected_indices[i];
                    if (idx < 0 || idx >= rs->card_count) {
                        valid = false;
                        break;
                    }
                    pass_cards[i] = rs->cards[idx].card;
                }
                if (!valid) break;
                game_state_select_pass(gs, 0, pass_cards);
                render_clear_selection(rs);
                finalize_card_pass(gs, rs);
            }
            break;

        case PHASE_PLAYING:
            if (cmd.type == INPUT_CMD_PLAY_CARD &&
                cmd.source_player == 0) {
                game_state_play_card(gs, 0, cmd.card.card);
                /* sync_needed will be set by flow_update */
            } else if (cmd.type == INPUT_CMD_SKIP_GRUDGE) {
                p2.players[0].grudge_token.used_this_round = true;
            } else if (cmd.type == INPUT_CMD_SELECT_GRUDGE_REVENGE) {
                grudge_apply_revenge(&p2, 0, cmd.grudge_revenge.revenge_id);
                {
                    const RevengeDef *rd =
                        phase2_get_revenge(cmd.grudge_revenge.revenge_id);
                    char rmsg[CHAT_MSG_LEN];
                    snprintf(rmsg, sizeof(rmsg), "Revenge: %s",
                             rd ? rd->name : "???");
                    render_chat_log_push(rs, rmsg);
                }
            } else if (cmd.type == INPUT_CMD_GRUDGE_DISCARD_CHOICE) {
                if (cmd.grudge_discard.keep_new) {
                    grudge_set_token(&p2, 0, rs->grudge_pending_attacker);
                }
                /* If keep_old, do nothing — old token stays */
                rs->grudge_discard_ui = false;
            }
            break;

        case PHASE_SCORING:
            if (cmd.type == INPUT_CMD_CONFIRM) {
                /* Evaluate and apply contract rewards before advancing */
                if (p2.enabled && !rs->show_contract_results) {
                    for (int i = 0; i < NUM_PLAYERS; i++) {
                        contract_evaluate(&p2, i);
                        contract_apply_reward(&p2, i);

                        /* Populate result text for display */
                        const ContractInstance *ci = &p2.players[i].contract;
                        if (ci->contract_id >= 0) {
                            const ContractDef *cd = phase2_get_contract(ci->contract_id);
                            const char *name = cd ? cd->name : "Unknown";
                            const char *status = ci->completed ? "Completed!" : "Failed";
                            snprintf(rs->contract_result_text[i],
                                     sizeof(rs->contract_result_text[i]),
                                     "%s: %s - %s",
                                     p2_player_name(i), name, status);
                            rs->contract_result_success[i] = ci->completed;
                        } else {
                            rs->contract_result_text[i][0] = '\0';
                        }
                    }
                    rs->show_contract_results = true;
                    /* Chat: contract results */
                    for (int j = 0; j < NUM_PLAYERS; j++) {
                        if (rs->contract_result_text[j][0] != '\0') {
                            render_chat_log_push(rs,
                                                 rs->contract_result_text[j]);
                        }
                    }
                } else {
                    rs->show_contract_results = false;
                    game_state_advance_scoring(gs);
                    rs->sync_needed = true;
                }
                /* Consume remaining commands to prevent double-advance in
                 * the same frame (e.g. Enter + mouse click simultaneously) */
                input_cmd_queue_clear();
                goto done_processing;
            }
            break;

        case PHASE_GAME_OVER:
            if (cmd.type == INPUT_CMD_CONFIRM) {
                host_action_ui_active = false;
                game_state_reset_to_menu(gs);
                rs->sync_needed = true;
            }
            break;

        default:
            break;
        }
    }
done_processing:
    (void)0;
}

/* ---- Entry point ---- */
int main(void)
{
    /* Load settings before window creation to get resolution */
    settings_load(&g_settings);

    int init_w = RESOLUTIONS[g_settings.resolution_index].width;
    int init_h = RESOLUTIONS[g_settings.resolution_index].height;
    InitWindow(init_w, init_h, WINDOW_TITLE);

    GameClock clk;
    clock_init(&clk);

    GameState gs;
    game_state_init(&gs);
    input_init();

    phase2_defs_init();
    contract_state_init(&p2);
    grudge_state_init(&p2);
    p2.enabled = true;

    RenderState rs;
    render_init(&rs);

    /* Apply loaded settings (fullscreen, fps, layout) */
    settings_apply(&g_settings, &rs.layout);

    card_render_init();

    TurnFlow flow;
    flow_init(&flow);
    GamePhase prev_phase = gs.phase;
    bool prev_hearts_broken = false;

    bool quit_requested = false;

    while (!WindowShouldClose() && !quit_requested) {
        clock_update(&clk);
        process_input(&gs, &rs, flow.step);

        while (clk.accumulator >= FIXED_DT) {
            update(&gs, &rs, FIXED_DT, &quit_requested);
            clk.accumulator -= FIXED_DT;
        }

        /* Reset flow when entering PHASE_PLAYING */
        if (gs.phase == PHASE_PLAYING && prev_phase != PHASE_PLAYING) {
            flow_init(&flow);
            rs.sync_needed = true;
        }

        /* Set up Phase 2 subphases when entering PHASE_PASSING */
        if (gs.phase == PHASE_PASSING && prev_phase != PHASE_PASSING) {
            pass_subphase_timer = 0.0f;
            pass_sub_ai_host = false;
            host_action_ui_active = false;

            if (p2.enabled) {
                contract_round_reset(&p2);
                grudge_round_reset(&p2);
                host_action_round_reset(&p2);
                p2.round.host_player_id =
                    host_action_determine_host(&gs, gs.round_number);

                int host_id = p2.round.host_player_id;
                if (host_id >= 0) {
                    /* Host exists — start with host action subphase */
                    advance_pass_subphase(&gs, &rs, PASS_SUB_HOST_ACTION);
                } else {
                    /* No host — skip to contract selection */
                    advance_pass_subphase(&gs, &rs, PASS_SUB_CONTRACT);
                }
            } else {
                /* Phase2 disabled — skip to card pass */
                pass_subphase = PASS_SUB_CARD_PASS;
                rs.pass_subphase = PASS_SUB_CARD_PASS;
                rs.pass_subphase_remaining = PASS_CARD_PASS_TIME;
                rs.pass_status_text = NULL;
                rs.contract_ui_active = false;

                if (gs.pass_direction == PASS_NONE) {
                    /* No passing and no p2 — go straight to PLAYING */
                    gs.phase = PHASE_PLAYING;
                    rs.sync_needed = true;
                }
            }
        }

        /* Chat log: round start */
        if (gs.phase == PHASE_PASSING && prev_phase != PHASE_PASSING) {
            char msg[CHAT_MSG_LEN];
            snprintf(msg, sizeof(msg), "-- Round %d --", gs.round_number);
            render_chat_log_push(&rs, msg);
        }

        /* Chat log: hearts broken */
        if (gs.hearts_broken && !prev_hearts_broken) {
            render_chat_log_push(&rs, "Hearts Broken!");
        }

        prev_phase = gs.phase;

        /* Sync grudge display data */
        if (p2.enabled) {
            for (int i = 0; i < NUM_PLAYERS; i++) {
                rs.player_has_grudge[i] = p2.players[i].grudge_token.active;
                rs.player_grudge_attacker[i] =
                    p2.players[i].grudge_token.attacker_id;
            }
        }

        /* Sync info panel */
        if (p2.enabled) {
            /* Contract */
            if (p2.players[0].contract.contract_id >= 0) {
                const ContractDef *cd =
                    phase2_get_contract(p2.players[0].contract.contract_id);
                if (cd) {
                    rs.info_contract_active = true;
                    snprintf(rs.info_contract_name,
                             sizeof(rs.info_contract_name), "%s", cd->name);
                    snprintf(rs.info_contract_desc,
                             sizeof(rs.info_contract_desc), "%s",
                             cd->description);
                } else {
                    rs.info_contract_active = false;
                }
            } else {
                rs.info_contract_active = false;
            }

            /* Host action */
            if (p2.round.chosen_host_action >= 0) {
                const HostActionDef *ha =
                    phase2_get_host_action(p2.round.chosen_host_action);
                if (ha) {
                    rs.info_host_active = true;
                    snprintf(rs.info_host_name,
                             sizeof(rs.info_host_name), "%s", ha->name);
                    snprintf(rs.info_host_desc,
                             sizeof(rs.info_host_desc), "%s",
                             ha->description);
                } else {
                    rs.info_host_active = false;
                }
            } else {
                rs.info_host_active = false;
            }

            /* Bonuses (persistent effects) */
            rs.info_bonus_count = 0;
            for (int i = 0;
                 i < p2.players[0].num_persistent && i < INFO_BONUS_MAX;
                 i++) {
                if (!p2.players[0].persistent_effects[i].active) continue;
                render_effect_label(
                    &p2.players[0].persistent_effects[i],
                    rs.info_bonus_text[rs.info_bonus_count], 48);
                rs.info_bonus_count++;
            }

            /* Grudge revenge options */
            if (p2.players[0].grudge_token.active &&
                !p2.players[0].grudge_token.used_this_round) {
                rs.info_grudge_available = true;
                snprintf(rs.info_grudge_attacker_name, 16, "%s",
                         p2_player_name(
                             p2.players[0].grudge_token.attacker_id));
                int ids[4];
                int cnt = grudge_get_revenge_options(&p2, 0, ids);
                rs.info_revenge_count = cnt;
                for (int i = 0; i < cnt; i++) {
                    rs.info_revenge_ids[i] = ids[i];
                    const RevengeDef *rd = phase2_get_revenge(ids[i]);
                    rs.info_revenge_btns[i].label =
                        rd ? rd->name : "???";
                    rs.info_revenge_btns[i].subtitle =
                        rd ? rd->description : "";
                    rs.info_revenge_btns[i].visible = true;
                    rs.info_revenge_btns[i].disabled =
                        !rs.info_grudge_interactive;
                }
                rs.info_revenge_skip_btn.label = "Skip";
                rs.info_revenge_skip_btn.visible = true;
                rs.info_revenge_skip_btn.disabled =
                    !rs.info_grudge_interactive;
            } else {
                rs.info_grudge_available = false;
                rs.info_revenge_count = 0;
                rs.info_revenge_skip_btn.visible = false;
            }
        } else {
            rs.info_contract_active = false;
            rs.info_host_active = false;
            rs.info_bonus_count = 0;
            rs.info_grudge_available = false;
            rs.info_revenge_count = 0;
        }

        /* Pass subphase timers (real time, UI deadlines) */
        pass_subphase_update(&gs, &rs, clk.raw_dt);

        /* Flow runs on raw_dt (real time) intentionally — it controls
           visual pacing, not game logic. Game state mutations happen
           inside flow_update only when timers expire. */
        flow_update(&flow, &gs, &rs, clk.raw_dt);

        /* Set interactive flag after flow_update to reflect final state */
        rs.info_grudge_interactive = (flow.step == FLOW_WAITING_FOR_HUMAN);

        render_update(&gs, &rs, clk.raw_dt);

        /* Particle burst: hearts broken (after render_update so trick_visuals are synced) */
        if (gs.hearts_broken && !prev_hearts_broken) {
            for (int ti = 0; ti < rs.trick_visual_count; ti++) {
                int idx = rs.trick_visuals[ti];
                if (idx < 0 || idx >= rs.card_count) continue;
                if (rs.cards[idx].card.suit != SUIT_HEARTS) continue;
                CardVisual *cv = &rs.cards[idx];
                Vector2 pos = cv->animating ? cv->target : cv->position;
                Vector2 center = {
                    pos.x + rs.layout.card_width * 0.5f,
                    pos.y + rs.layout.card_height * 0.5f
                };
                particle_spawn_burst(&rs.particles, center, 48);
                break;
            }
        }
        prev_hearts_broken = gs.hearts_broken;

        render_draw(&gs, &rs);
    }

    card_render_shutdown();
    CloseWindow();
    return 0;
}
