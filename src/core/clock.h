#ifndef CLOCK_H
#define CLOCK_H

/* ============================================================
 * @deps-exports: GameClock, FIXED_DT, MAX_FRAME_DT, MAX_CATCHUP,
 *                clock_init(), clock_update()
 * @deps-requires: raylib.h
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include <stdbool.h>

#define FIXED_DT        (1.0f / 60.0f)
#define MAX_FRAME_DT    0.25f
#define MAX_CATCHUP     5

typedef struct GameClock {
    float raw_dt;
    float dt;
    float accumulator;
    float total_time;
    float time_scale;
    bool  paused;
} GameClock;

void clock_init(GameClock *clk);
void clock_update(GameClock *clk);

#endif /* CLOCK_H */
