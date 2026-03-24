# 13 — Resource Management

## What Is a Resource Manager?

A unified system for loading, caching, and managing game assets: textures, sounds, music, map data, configuration files. Without one, every subsystem loads files independently, leading to duplicated assets in memory and inconsistent loading/unloading.

## Core Responsibilities

1. **Load assets from disk** (or from a packed archive)
2. **Cache loaded assets** so duplicates are never loaded twice
3. **Provide fast lookup** by name or ID
4. **Unload assets** when no longer needed (level transition)
5. **Handle errors** gracefully (missing files, corrupt data)

## Simple Resource Manager in C

For a 2D RPG, the primary asset types are textures, sounds, music tracks, and data files (maps, configs).

```c
#define MAX_TEXTURES 128
#define MAX_SOUNDS   64
#define MAX_MUSIC    8

typedef struct TextureEntry {
    uint32_t  name_hash;     // hashed filename for fast lookup
    char      path[128];     // original path
    Texture2D texture;
    bool      loaded;
} TextureEntry;

typedef struct SoundEntry {
    uint32_t  name_hash;
    char      path[128];
    Sound     sound;
    bool      loaded;
} SoundEntry;

typedef struct ResourceManager {
    TextureEntry textures[MAX_TEXTURES];
    int          texture_count;
    
    SoundEntry   sounds[MAX_SOUNDS];
    int          sound_count;
} ResourceManager;

ResourceManager g_resources = {0};
```

### String Hashing

Use a fast hash function to convert filenames to integer IDs:

```c
// FNV-1a hash (fast, good distribution)
uint32_t hash_string(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}
```

### Texture Loading with Cache

```c
Texture2D* resource_get_texture(const char* path) {
    uint32_t hash = hash_string(path);
    
    // Check if already loaded
    for (int i = 0; i < g_resources.texture_count; i++) {
        if (g_resources.textures[i].name_hash == hash && g_resources.textures[i].loaded) {
            return &g_resources.textures[i].texture;
        }
    }
    
    // Load new texture
    ASSERT(g_resources.texture_count < MAX_TEXTURES);
    TextureEntry* entry = &g_resources.textures[g_resources.texture_count++];
    entry->name_hash = hash;
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->texture = LoadTexture(path);
    
    if (entry->texture.id == 0) {
        LOG_ERROR(LOG_CAT_RESOURCE, "Failed to load texture: %s", path);
        g_resources.texture_count--;
        return NULL;
    }
    
    entry->loaded = true;
    LOG_DEBUG(LOG_CAT_RESOURCE, "Loaded texture: %s (%dx%d)", 
              path, entry->texture.width, entry->texture.height);
    return &entry->texture;
}
```

### Sound Loading

```c
Sound* resource_get_sound(const char* path) {
    uint32_t hash = hash_string(path);
    
    for (int i = 0; i < g_resources.sound_count; i++) {
        if (g_resources.sounds[i].name_hash == hash && g_resources.sounds[i].loaded) {
            return &g_resources.sounds[i].sound;
        }
    }
    
    ASSERT(g_resources.sound_count < MAX_SOUNDS);
    SoundEntry* entry = &g_resources.sounds[g_resources.sound_count++];
    entry->name_hash = hash;
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->sound = LoadSound(path);
    entry->loaded = true;
    return &entry->sound;
}
```

### Bulk Unloading

On level transition, unload all resources and reload for the new level:

```c
void resource_unload_all_textures(void) {
    for (int i = 0; i < g_resources.texture_count; i++) {
        if (g_resources.textures[i].loaded) {
            UnloadTexture(g_resources.textures[i].texture);
            g_resources.textures[i].loaded = false;
        }
    }
    g_resources.texture_count = 0;
}

void resource_unload_all_sounds(void) {
    for (int i = 0; i < g_resources.sound_count; i++) {
        if (g_resources.sounds[i].loaded) {
            UnloadSound(g_resources.sounds[i].sound);
            g_resources.sounds[i].loaded = false;
        }
    }
    g_resources.sound_count = 0;
}
```

## Asset Pipeline Concepts

**Offline processing** transforms raw assets into game-ready formats before the game runs:

1. **Tileset images** → packed sprite sheets with consistent tile sizes
2. **Tiled map files (.tmx)** → binary format optimized for fast loading
3. **Sound files** → consistent format (OGG for music, WAV for SFX)
4. **Data files** → convert JSON/CSV to binary for fast parsing

For a small project, you can skip the offline pipeline and load raw files directly. Add conversion steps only when loading times become a problem.

## File Organization

```
resources/
├── textures/
│   ├── player.png
│   ├── enemies.png          (sprite sheet)
│   ├── tileset_dungeon.png
│   ├── tileset_overworld.png
│   ├── items.png             (sprite sheet)
│   └── ui/
│       ├── hud.png
│       └── menu.png
├── sounds/
│   ├── sword_hit.wav
│   ├── player_hurt.wav
│   └── door_open.wav
├── music/
│   ├── overworld.ogg
│   └── dungeon.ogg
├── maps/
│   ├── level_01.map
│   └── level_02.map
└── data/
    ├── enemies.dat           (enemy type definitions)
    ├── items.dat             (item definitions)
    └── config.ini            (game settings)
```

## Reference Counting (Optional)

If multiple levels share some assets, use reference counting to avoid unloading assets that are still needed:

```c
typedef struct TextureEntry {
    uint32_t  name_hash;
    Texture2D texture;
    int       ref_count;
    bool      loaded;
} TextureEntry;

Texture2D* resource_acquire_texture(const char* path) {
    // ... lookup or load ...
    entry->ref_count++;
    return &entry->texture;
}

void resource_release_texture(Texture2D* tex) {
    // Find entry by pointer
    for (int i = 0; i < g_resources.texture_count; i++) {
        if (&g_resources.textures[i].texture == tex) {
            g_resources.textures[i].ref_count--;
            if (g_resources.textures[i].ref_count <= 0) {
                UnloadTexture(*tex);
                g_resources.textures[i].loaded = false;
            }
            return;
        }
    }
}
```

For a single-player RPG with discrete levels, bulk unload/reload is simpler and sufficient.

## Practical Tips

- **Load all level resources at once** during the loading screen. Avoid loading during gameplay — it causes frame stutters.
- **Use Raylib's built-in loaders**: `LoadTexture`, `LoadSound`, `LoadMusicStream`, `LoadFileData`. Don't reimplement file I/O.
- **Hash filenames for fast lookup**, but keep the original path for error messages and debugging.
- **Pre-define texture regions**: For sprite sheets, define named source rectangles so game code references sprites by name, not pixel coordinates.
