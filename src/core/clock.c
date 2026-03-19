/* ============================================================
 * @deps-implements: clock.h
 * @deps-requires: clock.h, raylib.h
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "clock.h"

#include "raylib.h"

void clock_init(GameClock *clk)
{
    clk->raw_dt      = 0.0f;
    clk->dt          = 0.0f;
    clk->accumulator = 0.0f;
    clk->total_time  = 0.0f;
    clk->time_scale  = 1.0f;
    clk->paused      = false;
}

void clock_update(GameClock *clk)
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
