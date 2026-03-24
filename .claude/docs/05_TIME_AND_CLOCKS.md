# 05 — Time and Clocks

## Abstract Timelines

Think of time as separate, independent timelines that can be mapped to each other:

- **Real time**: Actual wall-clock time. Raylib's `GetTime()` returns this.
- **Game time**: Can be paused, slowed, sped up. Drives gameplay logic.
- **Animation time**: Local to each animation clip. Can run at different speeds or in reverse.
- **UI time**: May keep running even when game time is paused (for animated menus).

## Clock Implementation in C

```c
typedef struct GameClock {
    double  total_time;     // total unpaused, scaled time in seconds
    float   dt;             // current frame's scaled delta
    float   raw_dt;         // unscaled (real-world) delta
    float   time_scale;     // 1.0 = normal, 0.5 = half-speed, 2.0 = double
    bool    paused;
} GameClock;

void clock_init(GameClock* c) {
    *c = (GameClock){ .time_scale = 1.0f, .paused = false };
}

void clock_update(GameClock* c, float real_dt) {
    c->raw_dt = real_dt;
    
    // Clamp extreme values (breakpoints, alt-tab lag)
    if (c->raw_dt > 0.1f) c->raw_dt = 1.0f / 60.0f;
    
    if (c->paused) {
        c->dt = 0.0f;
    } else {
        c->dt = c->raw_dt * c->time_scale;
        c->total_time += c->dt;
    }
}

void clock_set_paused(GameClock* c, bool paused) {
    c->paused = paused;
}

void clock_single_step(GameClock* c) {
    // Advance by one ideal frame while paused (for debugging)
    if (c->paused) {
        c->dt = (1.0f / 60.0f) * c->time_scale;
        c->total_time += c->dt;
    }
}
```

## Multiple Clocks

Maintain separate clocks for different purposes:

```c
GameClock g_real_clock;   // always runs (for UI, debug)
GameClock g_game_clock;   // pauses when game pauses
GameClock g_effect_clock; // may slow down for dramatic effect (hit-stop)

void clocks_update(void) {
    float real_dt = GetFrameTime();
    
    clock_update(&g_real_clock, real_dt);
    g_real_clock.paused = false; // real clock never pauses
    
    clock_update(&g_game_clock, real_dt);
    clock_update(&g_effect_clock, real_dt);
}
```

### Use cases:
- **Player movement, AI, physics**: use `g_game_clock.dt`
- **Menu animations, cursor blink**: use `g_real_clock.dt`
- **Hit-stop effect** (freeze for 0.05s on strong attack): pause `g_game_clock` briefly
- **Slow-motion**: set `g_game_clock.time_scale = 0.3f` temporarily

## Frame Rate and Time Deltas

| FPS | Frame period (dt) | Milliseconds |
|-----|-------------------|-------------|
| 60  | 0.01667s          | 16.67 ms    |
| 30  | 0.03333s          | 33.33 ms    |
| 120 | 0.00833s          | 8.33 ms     |

Raylib's `SetTargetFPS(60)` governs the frame rate. `GetFrameTime()` returns the actual measured dt.

## Time Units

For most game logic, **float seconds** (via `GetFrameTime()`) are perfectly fine. But be aware:

- **32-bit float precision degrades** with large magnitudes. After ~4.5 hours at 60 FPS, a float accumulating seconds starts losing sub-millisecond precision. For a single-session RPG this is unlikely to matter, but if it does, use `double` for total elapsed time.
- **Integer time** can be useful for deterministic replay or networking: represent time as fixed-point integers (e.g., 1/300s ticks). One tick = 3.33ms, wraps at ~165 days in uint32.

## Practical: Slow Motion and Hit-Stop

```c
// Hit-stop: freeze game for a few frames on a strong hit
typedef struct HitStop {
    float remaining; // seconds left in freeze
} HitStop;

HitStop g_hitstop = {0};

void hitstop_trigger(float duration) {
    g_hitstop.remaining = duration;
}

void hitstop_update(float real_dt) {
    if (g_hitstop.remaining > 0.0f) {
        g_hitstop.remaining -= real_dt;
        g_game_clock.paused = true;
    } else {
        g_game_clock.paused = false;
    }
}
```

```c
// Slow-motion for a dramatic dodge
void slowmo_start(void) {
    g_game_clock.time_scale = 0.25f;
}
void slowmo_stop(void) {
    g_game_clock.time_scale = 1.0f;
}
```

## Timer Utility

A simple countdown/countup timer:

```c
typedef struct Timer {
    float duration;
    float elapsed;
    bool  running;
    bool  looping;
} Timer;

void timer_start(Timer* t, float duration, bool looping) {
    t->duration = duration;
    t->elapsed  = 0.0f;
    t->running  = true;
    t->looping  = looping;
}

bool timer_update(Timer* t, float dt) {
    if (!t->running) return false;
    t->elapsed += dt;
    if (t->elapsed >= t->duration) {
        if (t->looping) {
            t->elapsed -= t->duration; // preserve remainder
        } else {
            t->elapsed = t->duration;
            t->running = false;
        }
        return true; // timer fired
    }
    return false;
}

float timer_progress(const Timer* t) {
    return (t->duration > 0.0f) ? (t->elapsed / t->duration) : 1.0f;
}
```

## Key Takeaways

1. Always pass `dt` to update functions — never assume a fixed frame time.
2. Use separate clocks for game logic vs. UI vs. effects.
3. Clamp `dt` to a maximum value to handle debugger breakpoints gracefully.
4. Store total elapsed time in `double` if the game session can be very long.
5. Use `float` for frame deltas and short durations — it has plenty of precision for values under a few minutes.
