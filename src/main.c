/* ============================================================
 * @deps-implements: (entry point)
 * @deps-requires: game_state.h (GameState, game_state_init), raylib.h
 * @deps-last-changed: 2026-03-14 — Core game loop skeleton
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "game_state.h"

/* ---- Constants ---- */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720
#define WINDOW_TITLE  "Hollow Hearts"
#define TARGET_FPS    60
#define FIXED_DT        (1.0f / 60.0f)   /* intentionally independent of TARGET_FPS */
#define MAX_FRAME_DT    0.25f
#define MAX_CATCHUP     5                /* max fixed-step iterations per frame */

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

/* ---- Game functions (stubs) ---- */
/* Buffer input only; do not modify game state directly.
 * State changes happen in update(). */
static void process_input(GameState *gs)
{
    (void)gs;
}

static void update(GameState *gs, float dt)
{
    (void)gs;
    (void)dt;
}

static void render(const GameState *gs)
{
    (void)gs;

    BeginDrawing();

    /* --- Game world pass --- */
    ClearBackground(DARKGREEN);

    /* --- UI overlay pass --- */
    {
        const char *title = WINDOW_TITLE;
        int font_size     = 40;
        int text_width    = MeasureText(title, font_size);
        DrawText(title,
                 (WINDOW_WIDTH - text_width) / 2,
                 WINDOW_HEIGHT / 2 - font_size / 2,
                 font_size, RAYWHITE);
    }

    DrawFPS(10, 10);

    EndDrawing();
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

    while (!WindowShouldClose()) {
        clock_update(&clk);
        process_input(&gs);

        while (clk.accumulator >= FIXED_DT) {
            update(&gs, FIXED_DT);
            clk.accumulator -= FIXED_DT;
        }

        render(&gs);
    }

    CloseWindow();
    return 0;
}
