# 14 — Data-Driven Design

## What Is Data-Driven Design?

When game behavior is controlled by external data files rather than hard-coded in source code. This allows designers (and developers) to change game behavior without recompiling.

**Examples**:
- Enemy stats (HP, speed, damage) in a CSV or JSON file
- Item definitions in a data file
- Level layouts in Tiled map files
- Key bindings in a config file
- Dialogue trees in a script/data file

## Benefits

- **Faster iteration**: Change a number in a file, restart the game, see the result
- **Separation of concerns**: Designers tweak balance without touching code
- **Hot reloading** (advanced): Reload data files while the game is running

## Costs

- **Parsing code**: You need code to read and validate data files
- **Error handling**: Invalid data must be caught gracefully, not crash the game
- **Tooling**: Complex data may need a visual editor

## When to Data-Drive and When Not To

**Data-drive**: Anything that will be tweaked frequently — stats, timings, spawn tables, dialogue, level layouts, audio volume, visual tuning parameters.

**Hard-code**: Core systems (game loop, rendering pipeline, collision), one-off gameplay mechanics that won't change, performance-critical inner loops.

**Guideline**: If a designer will ever ask "can you change X?", make X data-driven. But don't data-drive everything just because you can.

## Configuration Files

A simple key-value config file parser:

```c
// config.ini:
// window_width=1280
// window_height=720
// fullscreen=0
// master_volume=80
// music_volume=60

typedef struct Config {
    int  window_width;
    int  window_height;
    bool fullscreen;
    int  master_volume;
    int  music_volume;
    char start_level[64];
} Config;

Config g_config = {
    .window_width = 1280, .window_height = 720,
    .fullscreen = false, .master_volume = 80,
    .music_volume = 60,
};

void config_load(const char* path) {
    char* text = LoadFileText(path);
    if (!text) {
        LOG_WARN(LOG_CAT_ENGINE, "Config file not found: %s, using defaults", path);
        return;
    }
    
    char* line = strtok(text, "\n");
    while (line) {
        char key[64], value[64];
        if (sscanf(line, "%63[^=]=%63s", key, value) == 2) {
            if (strcmp(key, "window_width") == 0)   g_config.window_width = atoi(value);
            else if (strcmp(key, "window_height") == 0) g_config.window_height = atoi(value);
            else if (strcmp(key, "fullscreen") == 0)     g_config.fullscreen = atoi(value);
            else if (strcmp(key, "master_volume") == 0)  g_config.master_volume = atoi(value);
            else if (strcmp(key, "music_volume") == 0)   g_config.music_volume = atoi(value);
            else if (strcmp(key, "start_level") == 0)    strncpy(g_config.start_level, value, 63);
        }
        line = strtok(NULL, "\n");
    }
    
    UnloadFileText(text);
    LOG_INFO(LOG_CAT_ENGINE, "Config loaded: %dx%d, vol=%d", 
             g_config.window_width, g_config.window_height, g_config.master_volume);
}
```

## Enemy Definitions from Data

Define enemy types in an external file so you can add or tweak enemies without recompiling:

```c
// enemies.csv:
// name,health,speed,damage,aggro_radius,attack_cooldown,sprite
// goblin,30,80,5,120,1.0,enemies_goblin
// skeleton,50,60,8,100,1.5,enemies_skeleton
// slime,20,40,3,80,0.8,enemies_slime

typedef struct EnemyDef {
    char      name[32];
    float     health;
    float     speed;
    float     damage;
    float     aggro_radius;
    float     attack_cooldown;
    char      sprite_name[32];
} EnemyDef;

#define MAX_ENEMY_DEFS 64
EnemyDef g_enemy_defs[MAX_ENEMY_DEFS];
int      g_enemy_def_count = 0;

void load_enemy_definitions(const char* path) {
    char* text = LoadFileText(path);
    if (!text) return;
    
    g_enemy_def_count = 0;
    char* line = strtok(text, "\n");
    
    // Skip header
    line = strtok(NULL, "\n");
    
    while (line && g_enemy_def_count < MAX_ENEMY_DEFS) {
        EnemyDef* def = &g_enemy_defs[g_enemy_def_count];
        int parsed = sscanf(line, "%31[^,],%f,%f,%f,%f,%f,%31s",
            def->name, &def->health, &def->speed, &def->damage,
            &def->aggro_radius, &def->attack_cooldown, def->sprite_name);
        
        if (parsed == 7) {
            g_enemy_def_count++;
        }
        line = strtok(NULL, "\n");
    }
    
    UnloadFileText(text);
    LOG_INFO(LOG_CAT_RESOURCE, "Loaded %d enemy definitions", g_enemy_def_count);
}

EnemyDef* get_enemy_def(const char* name) {
    uint32_t hash = hash_string(name);
    for (int i = 0; i < g_enemy_def_count; i++) {
        if (hash_string(g_enemy_defs[i].name) == hash) {
            return &g_enemy_defs[i];
        }
    }
    LOG_ERROR(LOG_CAT_GAMEPLAY, "Unknown enemy type: %s", name);
    return NULL;
}
```

## Item Definitions

```c
typedef enum ItemType {
    ITEM_WEAPON,
    ITEM_ARMOR,
    ITEM_CONSUMABLE,
    ITEM_KEY,
    ITEM_QUEST,
} ItemType;

typedef struct ItemDef {
    uint32_t  id;
    char      name[32];
    ItemType  type;
    int       value;     // gold value
    int       stat_bonus; // damage for weapons, defense for armor, heal for consumables
    char      sprite[32];
    char      description[128];
} ItemDef;
```

## State Machines from Data (Optional, Advanced)

For complex AI or game flow, define state machines in data:

```
# patrol_enemy.fsm
state IDLE
  on PLAYER_SPOTTED -> CHASE
  on TIMER_EXPIRED -> PATROL
  
state PATROL
  on PLAYER_SPOTTED -> CHASE
  on REACHED_WAYPOINT -> IDLE
  
state CHASE
  on PLAYER_IN_RANGE -> ATTACK
  on PLAYER_LOST -> IDLE
  
state ATTACK
  on ATTACK_FINISHED -> CHASE
  on PLAYER_DIED -> IDLE
```

Parsing this requires more code, but it makes AI behaviors modifiable without recompilation.

## The KISS Principle

A critical warning from the book: many teams rush into data-driven design and overshoot dramatically, producing overly complex tools that are buggy and hard to use.

**Guidelines**:
1. Start by hard-coding. Only move to data-driven when you find yourself recompiling just to change a number.
2. Use the simplest data format that works. CSV is fine for flat tables. INI for config. Only use JSON or custom binary when you need nesting.
3. Don't build a visual editor until the data format is stable. You'll waste time if the format keeps changing.
4. Validate data on load. Print clear error messages for malformed data.
5. Provide sensible defaults. If a field is missing, use a default value rather than crashing.

## Practical Tips

- **Raylib's `LoadFileText` and `LoadFileData`** handle file I/O. Use them.
- **Keep data files in `resources/data/`** and track them in version control alongside code.
- **Log every data file load** with the number of entries parsed, so you can quickly spot loading failures.
- **Consider hot-reloading in debug builds**: Watch data files for changes and reload them without restarting the game. Even a simple "press F10 to reload data" shortcut is extremely valuable during development.
