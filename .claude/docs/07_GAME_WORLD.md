# 07 — Game World Structure

## Static vs Dynamic Elements

Every game world has two categories of elements:

- **Static**: Terrain, walls, floor tiles, decorative objects. They don't change state during gameplay. Can be precomputed and optimized.
- **Dynamic**: Player, enemies, projectiles, items, doors, switches, particles. They change state every frame and are the focus of gameplay logic.

The line between static and dynamic can blur. A breakable wall is static until the player attacks it. A door is static until triggered. Design for this by allowing static elements to be "promoted" to dynamic when needed.

## World Chunks (Levels / Rooms / Maps)

Large worlds are divided into chunks. Only one or a few chunks are loaded at a time. For a top-down RPG, chunks could be:
- Individual rooms in a dungeon
- Overworld regions (e.g., a 64x64 tile area)
- Connected zones with transitions (doors, stairs, portals)

```c
typedef struct WorldChunk {
    uint32_t   id;
    char       name[64];
    
    // Tile data
    int        width;          // in tiles
    int        height;
    uint16_t*  tile_data;      // tile IDs, row-major
    uint16_t*  collision_data; // collision flags per tile
    
    // Entity spawn points (loaded from map file)
    SpawnPoint* spawn_points;
    int         spawn_count;
    
    // Exits to other chunks
    ChunkExit*  exits;
    int         exit_count;
    
    // Loaded resources
    Texture2D   tileset;
    bool        loaded;
} WorldChunk;

typedef struct SpawnPoint {
    Vector2         position;
    GameObjectType  type;
    uint32_t        variant;    // e.g., goblin vs skeleton
    // Additional properties as key-value pairs
} SpawnPoint;

typedef struct ChunkExit {
    Rectangle   trigger_area;
    uint32_t    target_chunk_id;
    Vector2     target_position;  // where to place player in new chunk
} ChunkExit;
```

## Tile-Based Worlds

For a top-down RPG, a tile-based approach is standard:

```c
#define TILE_SIZE 16

typedef enum TileFlags {
    TILE_SOLID     = 1 << 0,  // blocks movement
    TILE_WATER     = 1 << 1,  // slows movement
    TILE_DAMAGE    = 1 << 2,  // hurts player (lava, spikes)
    TILE_TRIGGER   = 1 << 3,  // activates something
} TileFlags;

// Get tile at world position
uint16_t get_tile_at(WorldChunk* chunk, float world_x, float world_y) {
    int tx = (int)(world_x / TILE_SIZE);
    int ty = (int)(world_y / TILE_SIZE);
    if (tx < 0 || tx >= chunk->width || ty < 0 || ty >= chunk->height) {
        return 0; // out of bounds = empty
    }
    return chunk->tile_data[ty * chunk->width + tx];
}

bool is_tile_solid(WorldChunk* chunk, float world_x, float world_y) {
    int tx = (int)(world_x / TILE_SIZE);
    int ty = (int)(world_y / TILE_SIZE);
    if (tx < 0 || tx >= chunk->width || ty < 0 || ty >= chunk->height) {
        return true; // out of bounds = solid
    }
    return (chunk->collision_data[ty * chunk->width + tx] & TILE_SOLID) != 0;
}
```

## Rendering Tile Maps

```c
void render_tilemap(WorldChunk* chunk, Camera2D camera) {
    // Only render tiles visible in the camera viewport
    int start_x = (int)((camera.target.x - GetScreenWidth() / 2.0f / camera.zoom) / TILE_SIZE);
    int start_y = (int)((camera.target.y - GetScreenHeight() / 2.0f / camera.zoom) / TILE_SIZE);
    int end_x   = start_x + (int)(GetScreenWidth() / camera.zoom / TILE_SIZE) + 2;
    int end_y   = start_y + (int)(GetScreenHeight() / camera.zoom / TILE_SIZE) + 2;
    
    // Clamp to chunk bounds
    if (start_x < 0) start_x = 0;
    if (start_y < 0) start_y = 0;
    if (end_x > chunk->width)  end_x = chunk->width;
    if (end_y > chunk->height) end_y = chunk->height;
    
    int tiles_per_row = chunk->tileset.width / TILE_SIZE;
    
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            uint16_t tile_id = chunk->tile_data[y * chunk->width + x];
            if (tile_id == 0) continue; // empty tile
            
            // Calculate source rect in tileset texture
            int src_x = (tile_id % tiles_per_row) * TILE_SIZE;
            int src_y = (tile_id / tiles_per_row) * TILE_SIZE;
            Rectangle src = { src_x, src_y, TILE_SIZE, TILE_SIZE };
            
            Vector2 dest = { x * TILE_SIZE, y * TILE_SIZE };
            DrawTextureRec(chunk->tileset, src, dest, WHITE);
        }
    }
}
```

## Level Loading and Transitions

```c
WorldChunk  g_current_chunk = {0};
WorldChunk  g_next_chunk    = {0};  // for preloading

void load_chunk(WorldChunk* chunk, const char* path) {
    // Read map file (e.g., JSON, Tiled .tmx export, or custom binary)
    unsigned char* data = LoadFileData(path, NULL);
    // ... parse tile data, spawn points, exits ...
    chunk->loaded = true;
    UnloadFileData(data);
    
    // Spawn entities defined in the map
    for (int i = 0; i < chunk->spawn_count; i++) {
        spawn_entity_from_point(&chunk->spawn_points[i]);
    }
}

void unload_chunk(WorldChunk* chunk) {
    // Destroy all entities belonging to this chunk
    destroy_chunk_entities(chunk->id);
    // Free allocated data
    // (or just clear the level arena if using arena allocation)
    chunk->loaded = false;
}

void transition_to_chunk(uint32_t chunk_id, Vector2 player_pos) {
    unload_chunk(&g_current_chunk);
    // Could show loading screen here
    char path[256];
    snprintf(path, sizeof(path), "resources/maps/chunk_%u.map", chunk_id);
    load_chunk(&g_current_chunk, path);
    
    // Place player at the target position
    set_player_position(player_pos);
}
```

## Spatial Queries

For finding nearby entities efficiently (e.g., enemies within aggro radius):

### Simple approach: brute-force iteration
Fine for < 200 entities:
```c
void find_entities_in_radius(Vector2 center, float radius, Entity** results, int* count, int max) {
    *count = 0;
    float r2 = radius * radius;
    for (uint32_t i = 0; i < g_entity_count && *count < max; i++) {
        if (!g_entities[i].active) continue;
        float dx = g_entities[i].position.x - center.x;
        float dy = g_entities[i].position.y - center.y;
        if (dx*dx + dy*dy <= r2) {
            results[(*count)++] = &g_entities[i];
        }
    }
}
```

### Grid-based spatial hash (for many entities)
Divide the world into cells. Each cell stores a list of entity IDs. Query only the cells that overlap the search area.

```c
#define CELL_SIZE 64  // pixels per grid cell
#define GRID_W 128
#define GRID_H 128
#define MAX_PER_CELL 16

typedef struct SpatialGrid {
    uint32_t cells[GRID_W * GRID_H][MAX_PER_CELL];
    uint8_t  counts[GRID_W * GRID_H];
} SpatialGrid;

SpatialGrid g_spatial;

void spatial_clear(void) {
    memset(g_spatial.counts, 0, sizeof(g_spatial.counts));
}

void spatial_insert(uint32_t entity_idx, Vector2 pos) {
    int cx = (int)(pos.x / CELL_SIZE);
    int cy = (int)(pos.y / CELL_SIZE);
    if (cx < 0 || cx >= GRID_W || cy < 0 || cy >= GRID_H) return;
    int cell = cy * GRID_W + cx;
    if (g_spatial.counts[cell] < MAX_PER_CELL) {
        g_spatial.cells[cell][g_spatial.counts[cell]++] = entity_idx;
    }
}

// Rebuild every frame after entities move
void spatial_rebuild(void) {
    spatial_clear();
    for (uint32_t i = 0; i < g_entity_count; i++) {
        if (g_entities[i].active) {
            spatial_insert(i, g_entities[i].position);
        }
    }
}
```

## Game State

"Game state" refers to the complete snapshot of all dynamic data at a point in time:
- All entity positions, health, states
- Player inventory, quest progress
- World flags (which doors are open, which chests are looted)

For save/load functionality, you need to serialize this state. Keeping it in flat arrays (as in the ECS approach) makes serialization straightforward.
