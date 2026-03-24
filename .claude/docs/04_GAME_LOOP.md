# 04 — The Game Loop

## Concept

A game is a real-time simulation. Every frame, the engine must: read input, update game state, render the scene, and swap buffers. The master loop that orchestrates this is the **game loop**.

## Simplest Game Loop (Pong-style)

```c
int main(void) {
    init_game();
    
    while (!WindowShouldClose()) {
        read_input();
        update_game_state();
        
        BeginDrawing();
        ClearBackground(BLACK);
        render_game();
        EndDrawing();
    }
    
    shutdown_game();
    return 0;
}
```

This works but has no concept of time — object speeds depend on frame rate.

## Delta Time: Making Games Frame-Rate Independent

Object speeds must be expressed in **units per second**, not units per frame. Multiply speed by the elapsed time since last frame (`dt`).

```
new_position = old_position + velocity * dt
```

Raylib provides `GetFrameTime()` which returns `dt` in seconds. Use it everywhere.

```c
void update_player(Player* p, float dt) {
    if (IsKeyDown(KEY_RIGHT)) p->position.x += p->speed * dt;
    if (IsKeyDown(KEY_LEFT))  p->position.x -= p->speed * dt;
    if (IsKeyDown(KEY_DOWN))  p->position.y += p->speed * dt;
    if (IsKeyDown(KEY_UP))    p->position.y -= p->speed * dt;
}
```

## Game Loop Architectural Styles

### Style 1: Variable Timestep (simplest)
Use last frame's measured `dt` as the timestep for this frame. This is what most engines do.

```c
while (!WindowShouldClose()) {
    float dt = GetFrameTime();
    
    poll_input();
    update_all_systems(dt);
    render();
}
```

**Problem**: `dt` is an estimate based on the previous frame. A sudden spike can cause physics instability. Mitigation: clamp `dt` to a maximum value.

```c
float dt = GetFrameTime();
if (dt > 0.1f) dt = 1.0f / 60.0f; // breakpoint or extreme lag: assume 60 FPS
```

### Style 2: Fixed Timestep with Accumulator (recommended for physics)
Decouple physics update rate from rendering rate. Physics always runs at a fixed interval (e.g., 1/60s). Rendering happens as fast as possible, interpolating between physics states.

```c
#define FIXED_DT (1.0f / 60.0f)

float accumulator = 0.0f;

while (!WindowShouldClose()) {
    float frame_dt = GetFrameTime();
    if (frame_dt > 0.25f) frame_dt = 0.25f; // clamp
    
    accumulator += frame_dt;
    
    poll_input();
    
    while (accumulator >= FIXED_DT) {
        update_physics(FIXED_DT);
        update_game_logic(FIXED_DT);
        accumulator -= FIXED_DT;
    }
    
    // Optional: interpolate rendering between states
    // float alpha = accumulator / FIXED_DT;
    
    BeginDrawing();
    ClearBackground(BLACK);
    render_game(/* alpha for interpolation */);
    EndDrawing();
}
```

### Style 3: Frame-Rate Governing
Lock to a target FPS. If a frame finishes early, sleep until the target time. Raylib's `SetTargetFPS(60)` does exactly this.

```c
SetTargetFPS(60); // Raylib handles the governing

while (!WindowShouldClose()) {
    float dt = GetFrameTime(); // will be ~1/60
    update(dt);
    render();
}
```

**Best for our RPG**: Use `SetTargetFPS(60)` and treat `dt` as variable (but usually ~16.6ms). Clamp `dt` to handle breakpoints during development.

## Recommended Game Loop for This Project

```c
// ---- game_loop.c ----

typedef struct GameClock {
    float    time_scale;   // 1.0 = normal, 0.5 = slow-mo, 0.0 = paused
    float    dt;           // scaled delta time this frame
    float    raw_dt;       // unscaled (real) delta time
    float    total_time;   // total game time elapsed (paused time excluded)
    bool     paused;
} GameClock;

GameClock g_clock = { .time_scale = 1.0f };

void clock_update(GameClock* clock) {
    clock->raw_dt = GetFrameTime();
    
    // Clamp to avoid spiral-of-death or breakpoint spikes
    if (clock->raw_dt > 0.1f) clock->raw_dt = 1.0f / 60.0f;
    
    if (clock->paused) {
        clock->dt = 0.0f;
    } else {
        clock->dt = clock->raw_dt * clock->time_scale;
        clock->total_time += clock->dt;
    }
}

// Main loop
int main(void) {
    engine_init();
    
    while (!WindowShouldClose()) {
        // 1. Time
        clock_update(&g_clock);
        
        // 2. Input
        input_system_update();
        
        // 3. Game logic update (uses g_clock.dt)
        game_objects_update(g_clock.dt);
        
        // 4. Post-update (resolve collisions, apply physics)
        collision_resolve();
        
        // 5. Late update (camera follow, particles)
        camera_update(g_clock.dt);
        particles_update(g_clock.dt);
        
        // 6. Render
        BeginDrawing();
        ClearBackground(BLACK);
        game_render();
        debug_render();         // debug overlays (collision boxes, etc.)
        hud_render();           // HUD on top
        EndDrawing();
        
        // 7. End-of-frame cleanup
        frame_alloc_reset();    // wipe per-frame allocations
        event_queue_dispatch(); // process deferred events
    }
    
    engine_shutdown();
    return 0;
}
```

## Subsystem Update Order Matters

The order in which you update subsystems within a single frame is critical. A typical correct order:

1. **Clock** — measure dt
2. **Input** — read devices, map to actions  
3. **Game objects** — AI, player logic, state machines
4. **Animation** — advance sprite animations
5. **Physics / collision** — move objects, resolve overlaps
6. **Camera** — follow player (after player has moved)
7. **Particles / VFX** — update effects
8. **Audio** — update positional audio, trigger sounds
9. **Render** — draw everything
10. **Events** — dispatch queued events for next frame
11. **Frame cleanup** — reset frame allocator

Changing this order can cause one-frame-off bugs. For example, if you render before updating the camera, the camera will always lag one frame behind the player.

## Handling Breakpoints During Development

When you hit a breakpoint in a debugger, the real clock keeps running. When you resume, `GetFrameTime()` returns a huge value, causing objects to teleport. The clamp on dt handles this:

```c
if (clock->raw_dt > 0.1f) clock->raw_dt = 1.0f / 60.0f;
```

This effectively frame-locks the game for one frame after resuming from a breakpoint.
