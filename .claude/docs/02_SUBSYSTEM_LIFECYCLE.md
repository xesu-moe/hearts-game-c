# 02 — Subsystem Start-Up and Shut-Down

## The Problem

A game engine has many subsystems with interdependencies. If subsystem B depends on A, then A must start before B and shut down after B. In C++, global object constructors/destructors execute in unpredictable order, which is dangerous. In C, we don't have this problem — but we still need explicit, ordered initialization.

## The Solution: Explicit Start-Up / Shut-Down Functions

The simplest approach (and the one used by professional engines including Naughty Dog's) is the "brute-force" method: define explicit `init()` and `shutdown()` functions for each subsystem and call them manually in the correct order from `main()`.

**Why this wins over "clever" solutions:**
- Simple and easy to implement
- Explicit — you can see and understand the order by reading the code
- Easy to debug — just move one line if something starts too early or late

### C Implementation

```c
// ---- subsystem_memory.h ----
typedef struct MemorySystem {
    void* main_arena;
    size_t arena_size;
    size_t arena_used;
} MemorySystem;

extern MemorySystem g_memory;
void memory_system_init(size_t arena_size_bytes);
void memory_system_shutdown(void);

// ---- subsystem_resources.h ----
typedef struct ResourceManager {
    // texture cache, sound cache, etc.
    int initialized;
} ResourceManager;

extern ResourceManager g_resources;
void resource_manager_init(void);
void resource_manager_shutdown(void);

// ---- subsystem_game_world.h ----
typedef struct GameWorld {
    // game objects, map data, etc.
    int initialized;
} GameWorld;

extern GameWorld g_world;
void game_world_init(void);
void game_world_shutdown(void);
```

```c
// ---- main.c ----
#include "subsystem_memory.h"
#include "subsystem_resources.h"
#include "subsystem_game_world.h"

// Global singleton instances
MemorySystem    g_memory    = {0};
ResourceManager g_resources = {0};
GameWorld       g_world     = {0};

int main(int argc, char* argv[])
{
    // === START-UP (dependency order) ===
    memory_system_init(64 * 1024 * 1024); // 64 MB arena
    
    InitWindow(1280, 720, "My RPG");      // Raylib
    InitAudioDevice();                     // Raylib
    SetTargetFPS(60);
    
    resource_manager_init();
    game_world_init();
    // ... more subsystems ...

    // === MAIN GAME LOOP ===
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        game_update(dt);
        
        BeginDrawing();
        ClearBackground(BLACK);
        game_render();
        EndDrawing();
    }

    // === SHUT-DOWN (reverse order) ===
    // ... more subsystems ...
    game_world_shutdown();
    resource_manager_shutdown();
    
    CloseAudioDevice();                    // Raylib
    CloseWindow();                         // Raylib
    
    memory_system_shutdown();

    return 0;
}
```

## Singleton Pattern in C

In C, a "singleton" is simply a global variable. No special patterns needed. Declare the struct in a header, define the global instance in one `.c` file, and access it via `extern`.

```c
// engine.h
typedef struct Engine {
    bool running;
    float time_scale;
    int target_fps;
} Engine;

extern Engine g_engine;

// engine.c
Engine g_engine = {
    .running = true,
    .time_scale = 1.0f,
    .target_fps = 60
};
```

## Practical Tips

1. **Zero-initialize all globals**: Use `= {0}` to ensure a known starting state.
2. **Add an `initialized` flag**: Each subsystem can carry a flag so you can assert that it was properly started before use.
3. **Never allocate in init if you can avoid it**: Prefer static arrays or arena allocation over `malloc` in init functions.
4. **Log each init step**: Print a message as each subsystem starts, so you can diagnose failures.
5. **Shut down in strict reverse order**: Never skip or reorder shutdown steps. Memory corruption bugs from out-of-order shutdown are extremely hard to find.

```c
void memory_system_init(size_t arena_size_bytes) {
    LOG_INFO("Memory system: initializing with %zu bytes", arena_size_bytes);
    g_memory.main_arena = malloc(arena_size_bytes);
    ASSERT(g_memory.main_arena != NULL);
    g_memory.arena_size = arena_size_bytes;
    g_memory.arena_used = 0;
    LOG_INFO("Memory system: OK");
}

void memory_system_shutdown(void) {
    LOG_INFO("Memory system: shutting down (used %zu / %zu bytes)",
             g_memory.arena_used, g_memory.arena_size);
    free(g_memory.main_arena);
    g_memory = (MemorySystem){0};
    LOG_INFO("Memory system: shut down");
}
```
