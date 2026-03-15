/* ============================================================
 * @deps-implements: (entry point)
 * @deps-requires: game_state.h (GameState, game_state_init, game_state_start_game),
 *                input.h (input_init, input_poll, input_cmd_pop, InputCmd),
 *                render.h (RenderState, render_init, render_update, render_draw,
 *                          render_hit_test_button, MenuItem, MENU_ITEM_COUNT),
 *                card_render.h (card_render_init, card_render_shutdown),
 *                raylib.h (InitWindow, SetTargetFPS, GetFrameTime,
 *                WindowShouldClose, CloseWindow)
 * @deps-last-changed: 2026-03-14 — Added menu_items loop with MenuItem enum for hit-testing, handle INPUT_CMD_QUIT
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "card_render.h"
#include "game_state.h"
#include "input.h"
#include "render.h"

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

static void flow_init(TurnFlow *flow)
{
    flow->step = FLOW_IDLE;
    flow->timer = 0.0f;
    flow->animating_player = -1;
    flow->prev_trick_count = 0;
}

static void flow_update(TurnFlow *flow, GameState *gs, RenderState *rs,
                         float dt);

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
    case FLOW_IDLE:
        /* Check if trick is complete */
        if (trick_is_complete(&gs->current_trick)) {
            flow->step = FLOW_TRICK_DISPLAY;
            flow->timer = FLOW_TRICK_DISPLAY_TIME;
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
                flow->timer = FLOW_AI_THINK_TIME;
            }
            /* current == -1 means no valid player; stay IDLE and
               let the phase-change guard at top handle it next frame */
        }
        break;

    case FLOW_WAITING_FOR_HUMAN:
        /* Detect if human played a card (num_played increased) */
        if (gs->current_trick.num_played > flow->prev_trick_count) {
            rs->sync_needed = true;
            rs->anim_play_player = 0;
            flow->step = FLOW_CARD_ANIMATING;
            flow->timer = FLOW_CARD_ANIM_TIME;
        }
        break;

    case FLOW_AI_THINKING:
        if (flow->timer <= 0.0f) {
            int current = game_state_current_player(gs);
            if (current > 0) {
                ai_play_card(gs, current);
                rs->sync_needed = true;
                rs->anim_play_player = current;
                flow->step = FLOW_CARD_ANIMATING;
                flow->timer = FLOW_CARD_ANIM_TIME;
            } else {
                /* Turn changed unexpectedly; re-evaluate */
                flow->step = FLOW_IDLE;
            }
        }
        break;

    case FLOW_CARD_ANIMATING:
        if (flow->timer <= 0.0f) {
            flow->step = FLOW_IDLE;
        }
        break;

    case FLOW_TRICK_DISPLAY:
        if (flow->timer <= 0.0f) {
            flow->step = FLOW_TRICK_COLLECTING;
            flow->timer = FLOW_TRICK_COLLECT_TIME;
        }
        break;

    case FLOW_TRICK_COLLECTING:
        if (flow->timer <= 0.0f) {
            game_state_complete_trick(gs);
            rs->sync_needed = true;
            flow->step = FLOW_BETWEEN_TRICKS;
            flow->timer = FLOW_BETWEEN_TRICKS_TIME;
        }
        break;

    case FLOW_BETWEEN_TRICKS:
        if (flow->timer <= 0.0f) {
            flow->step = FLOW_IDLE;
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
            /* Check confirm button */
            if (render_hit_test_button(&rs->btn_confirm_pass, mouse)) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_CONFIRM,
                    .source_player = 0,
                });
                break;
            }
            /* Check card click */
            int hit = render_hit_test_card(rs, mouse);
            if (hit >= 0) {
                render_toggle_card_selection(rs, hit);
            }
            break;
        }

        case PHASE_PLAYING: {
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
            } else if (cmd.type == INPUT_CMD_QUIT ||
                       cmd.type == INPUT_CMD_CANCEL) {
                *quit_requested = true;
            }
            break;

        case PHASE_DEALING:
            break;

        case PHASE_PASSING:
            if (cmd.type == INPUT_CMD_CONFIRM &&
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

                /* AI selects passes */
                for (int p = 1; p < NUM_PLAYERS; p++) {
                    if (!gs->pass_ready[p]) {
                        ai_select_pass(gs, p);
                    }
                }

                /* Execute pass if all ready */
                if (game_state_all_passes_ready(gs)) {
                    game_state_execute_pass(gs);
                    rs->sync_needed = true;
                }
            }
            break;

        case PHASE_PLAYING:
            if (cmd.type == INPUT_CMD_PLAY_CARD &&
                cmd.source_player == 0) {
                game_state_play_card(gs, 0, cmd.card.card);
                /* sync_needed will be set by flow_update */
            }
            break;

        case PHASE_SCORING:
            if (cmd.type == INPUT_CMD_CONFIRM) {
                game_state_advance_scoring(gs);
                rs->sync_needed = true;
            }
            break;

        case PHASE_GAME_OVER:
            if (cmd.type == INPUT_CMD_CONFIRM) {
                game_state_reset_to_menu(gs);
                rs->sync_needed = true;
            }
            break;

        default:
            break;
        }
    }

}

/* ---- Entry point ---- */
int main(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(TARGET_FPS);

    GameClock clk;
    clock_init(&clk);

    GameState gs;
    game_state_init(&gs);
    input_init();

    RenderState rs;
    render_init(&rs);
    card_render_init();

    TurnFlow flow;
    flow_init(&flow);
    GamePhase prev_phase = gs.phase;

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
        prev_phase = gs.phase;

        /* Flow runs on raw_dt (real time) intentionally — it controls
           visual pacing, not game logic. Game state mutations happen
           inside flow_update only when timers expire. */
        flow_update(&flow, &gs, &rs, clk.raw_dt);
        render_update(&gs, &rs, clk.raw_dt);
        render_draw(&gs, &rs);
    }

    card_render_shutdown();
    CloseWindow();
    return 0;
}
