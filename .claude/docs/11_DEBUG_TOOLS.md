# 11 — Debug and Development Tools

## Logging System

A categorized logging system with verbosity levels. Essential for debugging.

```c
typedef enum LogLevel {
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL,
} LogLevel;

typedef enum LogCategory {
    LOG_CAT_ENGINE = 0,
    LOG_CAT_GAMEPLAY,
    LOG_CAT_AI,
    LOG_CAT_PHYSICS,
    LOG_CAT_AUDIO,
    LOG_CAT_RESOURCE,
    LOG_CAT_COUNT
} LogCategory;

static LogLevel g_min_level = LOG_LEVEL_DEBUG;
static bool     g_category_enabled[LOG_CAT_COUNT] = { [0 ... LOG_CAT_COUNT-1] = true };
static FILE*    g_log_file = NULL;

void log_init(const char* log_path) {
    g_log_file = fopen(log_path, "w");
}

void log_shutdown(void) {
    if (g_log_file) fclose(g_log_file);
}

void log_message(LogLevel level, LogCategory cat, const char* file, int line, const char* fmt, ...) {
    if (level < g_min_level) return;
    if (!g_category_enabled[cat]) return;
    
    static const char* level_names[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };
    
    va_list args;
    va_start(args, fmt);
    
    // Print to stdout
    printf("[%s][%s:%d] ", level_names[level], file, line);
    vprintf(fmt, args);
    printf("\n");
    
    // Also write to log file
    if (g_log_file) {
        fprintf(g_log_file, "[%.3f][%s] ", GetTime(), level_names[level]);
        vfprintf(g_log_file, fmt, args);
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
    }
    
    va_end(args);
}

// Convenience macros
#define LOG_TRACE(cat, ...) log_message(LOG_LEVEL_TRACE, cat, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(cat, ...) log_message(LOG_LEVEL_DEBUG, cat, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(cat, ...)  log_message(LOG_LEVEL_INFO,  cat, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(cat, ...)  log_message(LOG_LEVEL_WARN,  cat, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(cat, ...) log_message(LOG_LEVEL_ERROR, cat, __FILE__, __LINE__, __VA_ARGS__)
```

## Assertions

Catch programming errors early. Strip from release builds.

```c
#ifdef DEBUG
    #define ASSERT(expr) do { \
        if (!(expr)) { \
            LOG_ERROR(LOG_CAT_ENGINE, "ASSERTION FAILED: %s", #expr); \
            __builtin_trap(); /* or abort() */ \
        } \
    } while(0)
#else
    #define ASSERT(expr) ((void)0)
#endif
```

## Debug Drawing

Overlay visual debug information on the game world. Toggle categories on/off at runtime.

```c
typedef enum DebugDrawFlags {
    DBGDRAW_COLLIDERS    = 1 << 0,
    DBGDRAW_ENTITY_IDS   = 1 << 1,
    DBGDRAW_PATHS        = 1 << 2,
    DBGDRAW_TRIGGERS     = 1 << 3,
    DBGDRAW_SPATIAL_GRID = 1 << 4,
    DBGDRAW_HEALTH_BARS  = 1 << 5,
    DBGDRAW_VELOCITY     = 1 << 6,
} DebugDrawFlags;

uint32_t g_debug_draw_flags = 0;

void debug_draw(void) {
    if (g_debug_draw_flags == 0) return;
    
    for (uint32_t i = 0; i < g_entity_count; i++) {
        Entity* e = &g_entities[i];
        if (!e->active) continue;
        
        if (g_debug_draw_flags & DBGDRAW_COLLIDERS) {
            Rectangle world_collider = {
                e->position.x + e->collider.x,
                e->position.y + e->collider.y,
                e->collider.width, e->collider.height
            };
            DrawRectangleLinesEx(world_collider, 1, GREEN);
        }
        
        if (g_debug_draw_flags & DBGDRAW_ENTITY_IDS) {
            DrawText(TextFormat("ID:%u", e->id), 
                     e->position.x, e->position.y - 12, 8, YELLOW);
        }
        
        if (g_debug_draw_flags & DBGDRAW_VELOCITY) {
            DrawLineV(e->position, 
                      Vector2Add(e->position, Vector2Scale(e->velocity, 0.1f)), 
                      RED);
        }
    }
}
```

## In-Game Console

A text input overlay for executing debug commands at runtime.

```c
typedef struct DebugConsole {
    bool   visible;
    char   input_buffer[256];
    int    cursor;
    char   history[16][256];
    int    history_count;
} DebugConsole;

DebugConsole g_console = {0};

typedef void (*ConsoleCmdFn)(const char* args);

typedef struct ConsoleCommand {
    const char*  name;
    ConsoleCmdFn handler;
    const char*  help;
} ConsoleCommand;

// Register commands
void cmd_god(const char* args)       { g_player_godmode = !g_player_godmode; }
void cmd_speed(const char* args)     { g_player_speed = atof(args); }
void cmd_spawn(const char* args)     { /* parse entity type and spawn */ }
void cmd_tp(const char* args)        { /* teleport player to x,y */ }
void cmd_kill_all(const char* args)  { /* destroy all enemies */ }
void cmd_debug(const char* args)     { g_debug_draw_flags ^= atoi(args); }

ConsoleCommand g_commands[] = {
    { "god",      cmd_god,      "Toggle god mode" },
    { "speed",    cmd_speed,    "Set player speed" },
    { "spawn",    cmd_spawn,    "Spawn entity: spawn <type>" },
    { "tp",       cmd_tp,       "Teleport: tp <x> <y>" },
    { "killall",  cmd_kill_all, "Kill all enemies" },
    { "debug",    cmd_debug,    "Toggle debug flags: debug <bitmask>" },
};
```

## Profiling

Measure time spent in each subsystem per frame. Display as an overlay.

```c
typedef struct ProfileBlock {
    const char* name;
    double      start_time;
    double      duration_ms;
} ProfileBlock;

#define MAX_PROFILE_BLOCKS 32
ProfileBlock g_profile_blocks[MAX_PROFILE_BLOCKS];
int          g_profile_count = 0;

void profile_begin(const char* name) {
    if (g_profile_count < MAX_PROFILE_BLOCKS) {
        g_profile_blocks[g_profile_count].name = name;
        g_profile_blocks[g_profile_count].start_time = GetTime();
        g_profile_count++;
    }
}

void profile_end(void) {
    if (g_profile_count > 0) {
        g_profile_count--;
        g_profile_blocks[g_profile_count].duration_ms = 
            (GetTime() - g_profile_blocks[g_profile_count].start_time) * 1000.0;
    }
}

// Usage in game loop:
// profile_begin("GameObjects");
// update_game_objects(dt);
// profile_end();

void profile_draw_overlay(void) {
    int y = 10;
    DrawText(TextFormat("FPS: %d", GetFPS()), 10, y, 16, WHITE); y += 20;
    
    for (int i = 0; i < MAX_PROFILE_BLOCKS; i++) {
        if (g_profile_blocks[i].name) {
            DrawText(TextFormat("%-16s: %.2f ms", 
                     g_profile_blocks[i].name, 
                     g_profile_blocks[i].duration_ms), 
                     10, y, 12, LIME);
            y += 14;
        }
    }
}
```

## Debug Camera

A fly-through camera for inspecting the world, independent of the player:

```c
typedef struct DebugCamera {
    bool     active;
    Camera2D camera;
    float    move_speed;
} DebugCamera;

void debug_camera_update(DebugCamera* dc, float dt) {
    if (!dc->active) return;
    
    float spd = dc->move_speed * dt;
    if (IsKeyDown(KEY_RIGHT)) dc->camera.target.x += spd;
    if (IsKeyDown(KEY_LEFT))  dc->camera.target.x -= spd;
    if (IsKeyDown(KEY_DOWN))  dc->camera.target.y += spd;
    if (IsKeyDown(KEY_UP))    dc->camera.target.y -= spd;
    
    // Zoom with mouse wheel
    float wheel = GetMouseWheelMove();
    dc->camera.zoom += wheel * 0.1f;
    if (dc->camera.zoom < 0.1f) dc->camera.zoom = 0.1f;
}
```

## Cheat Keys (Development Only)

```c
#ifdef DEBUG
void process_cheat_keys(void) {
    if (IsKeyPressed(KEY_F1)) g_debug_draw_flags ^= DBGDRAW_COLLIDERS;
    if (IsKeyPressed(KEY_F2)) g_debug_draw_flags ^= DBGDRAW_ENTITY_IDS;
    if (IsKeyPressed(KEY_F3)) g_debug_draw_flags ^= DBGDRAW_SPATIAL_GRID;
    if (IsKeyPressed(KEY_F5)) g_player_godmode = !g_player_godmode;
    if (IsKeyPressed(KEY_F6)) heal_player_full();
    if (IsKeyPressed(KEY_F7)) give_all_items();
    if (IsKeyPressed(KEY_F8)) skip_to_next_level();
    if (IsKeyPressed(KEY_F9)) g_game_clock.time_scale = (g_game_clock.time_scale == 1.0f) ? 0.25f : 1.0f;
    if (IsKeyPressed(KEY_GRAVE)) g_console.visible = !g_console.visible;
}
#endif
```

## Screenshot Capture

```c
void take_screenshot(void) {
    char filename[128];
    snprintf(filename, sizeof(filename), "screenshot_%04d.png", g_screenshot_counter++);
    TakeScreenshot(filename); // Raylib built-in
    LOG_INFO(LOG_CAT_ENGINE, "Screenshot saved: %s", filename);
}
```

## Practical Tips

- **Debug tools are not optional**: Build them from day one. They pay for themselves within a week of development.
- **Compile them out for release**: Wrap all debug code in `#ifdef DEBUG` blocks.
- **Make debug toggles keyboard-accessible**: You need to toggle things quickly while testing, not dig through menus.
- **Log to file AND console**: Console output scrolls away. The file persists for post-mortem analysis.
