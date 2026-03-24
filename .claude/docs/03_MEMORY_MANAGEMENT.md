# 03 — Memory Management

## Why Custom Allocators?

`malloc`/`free` are general-purpose and therefore slow. They also fragment memory over time. Game engines use custom allocators to:
1. **Avoid heap overhead**: Allocate from pre-reserved blocks (no OS context switch)
2. **Prevent fragmentation**: Controlled allocation patterns keep memory tidy
3. **Improve cache performance**: Keep related data contiguous in RAM
4. **Enable bulk freeing**: Wipe an entire level's data in one operation

**Rule of thumb**: Keep heap allocations to a minimum. Never allocate from the heap in a tight loop.

## Stack Allocator (Arena Allocator)

The most useful allocator for game engines. Allocate a big block up front, hand out memory by bumping a pointer. Free by rolling back to a saved marker. No individual frees.

Perfect for: level data, per-frame temporary data, loading buffers.

```c
typedef uint32_t ArenaMarker;

typedef struct Arena {
    uint8_t* base;       // start of the memory block
    size_t   size;       // total capacity in bytes
    size_t   offset;     // current allocation offset (the "top")
} Arena;

void arena_init(Arena* arena, void* backing_memory, size_t size) {
    arena->base   = (uint8_t*)backing_memory;
    arena->size   = size;
    arena->offset = 0;
}

void* arena_alloc(Arena* arena, size_t size_bytes) {
    // Align to 8 bytes for safety
    size_t aligned = (size_bytes + 7) & ~(size_t)7;
    ASSERT(arena->offset + aligned <= arena->size && "Arena out of memory");
    void* ptr = arena->base + arena->offset;
    arena->offset += aligned;
    return ptr;
}

ArenaMarker arena_get_marker(Arena* arena) {
    return (ArenaMarker)arena->offset;
}

void arena_free_to_marker(Arena* arena, ArenaMarker marker) {
    ASSERT(marker <= arena->offset);
    arena->offset = marker;
}

void arena_clear(Arena* arena) {
    arena->offset = 0;
}
```

### Usage Example: Level Loading

```c
static uint8_t level_memory[16 * 1024 * 1024]; // 16 MB static buffer
static Arena   level_arena;

void load_level(const char* path) {
    arena_clear(&level_arena); // wipe previous level
    
    // All level data allocated from this arena
    TileMap* map = arena_alloc(&level_arena, sizeof(TileMap));
    map->tiles   = arena_alloc(&level_arena, map->width * map->height * sizeof(Tile));
    // ... load data into the allocated memory ...
}
```

## Pool Allocator

For allocating many same-sized objects (entities, projectiles, particles). O(1) alloc and free via a free-list embedded in the free blocks themselves.

```c
typedef struct Pool {
    uint8_t* memory;       // backing memory
    size_t   element_size; // size of each element (>= sizeof(void*))
    size_t   count;        // total number of elements
    void*    free_head;    // head of the embedded free list
} Pool;

void pool_init(Pool* pool, void* backing_memory, size_t element_size, size_t count) {
    ASSERT(element_size >= sizeof(void*)); // must fit a pointer
    pool->memory       = (uint8_t*)backing_memory;
    pool->element_size = element_size;
    pool->count        = count;
    
    // Build the free list: each free block stores a pointer to the next
    pool->free_head = pool->memory;
    for (size_t i = 0; i < count - 1; i++) {
        void* current = pool->memory + i * element_size;
        void* next    = pool->memory + (i + 1) * element_size;
        *(void**)current = next; // embed "next" pointer in the free block
    }
    // Last element points to NULL
    void* last = pool->memory + (count - 1) * element_size;
    *(void**)last = NULL;
}

void* pool_alloc(Pool* pool) {
    if (pool->free_head == NULL) return NULL; // pool exhausted
    void* block = pool->free_head;
    pool->free_head = *(void**)block; // advance free list
    return block;
}

void pool_free(Pool* pool, void* block) {
    *(void**)block = pool->free_head; // prepend to free list
    pool->free_head = block;
}
```

### Usage Example: Projectile Pool

```c
#define MAX_PROJECTILES 256

typedef struct Projectile {
    Vector2 position;
    Vector2 velocity;
    float   lifetime;
    int     damage;
    bool    active;
} Projectile;

static uint8_t     projectile_memory[MAX_PROJECTILES * sizeof(Projectile)];
static Pool        projectile_pool;

void projectiles_init(void) {
    pool_init(&projectile_pool, projectile_memory, sizeof(Projectile), MAX_PROJECTILES);
}

Projectile* projectile_spawn(Vector2 pos, Vector2 vel, int damage) {
    Projectile* p = (Projectile*)pool_alloc(&projectile_pool);
    if (!p) return NULL; // pool full
    *p = (Projectile){ .position = pos, .velocity = vel, .damage = damage, 
                        .lifetime = 3.0f, .active = true };
    return p;
}

void projectile_destroy(Projectile* p) {
    p->active = false;
    pool_free(&projectile_pool, p);
}
```

## Single-Frame Allocator

A stack allocator that is cleared every frame. Allocate temporary data freely; it all vanishes at the start of the next frame. Never store pointers to frame-allocated data across frame boundaries.

```c
static uint8_t frame_buffer[1 * 1024 * 1024]; // 1 MB per frame
static Arena   frame_arena;

void frame_alloc_init(void) {
    arena_init(&frame_arena, frame_buffer, sizeof(frame_buffer));
}

void* frame_alloc(size_t size) {
    return arena_alloc(&frame_arena, size);
}

// Called at the top of every frame
void frame_alloc_reset(void) {
    arena_clear(&frame_arena);
}
```

Use for: temporary collision result arrays, formatted strings for debug display, scratch calculation buffers.

## Double-Buffered Allocator

Allows data allocated on frame N to survive until frame N+2. Useful for caching results of asynchronous work.

```c
typedef struct DoubleBufferedAllocator {
    Arena  arenas[2];
    int    current; // 0 or 1
} DoubleBufferedAllocator;

void dba_swap_and_clear(DoubleBufferedAllocator* dba) {
    dba->current = 1 - dba->current;
    arena_clear(&dba->arenas[dba->current]);
}

void* dba_alloc(DoubleBufferedAllocator* dba, size_t size) {
    return arena_alloc(&dba->arenas[dba->current], size);
}
```

## Memory Fragmentation

**The problem**: After many allocs and frees of varying sizes, the heap develops "holes" — free blocks too small to satisfy larger requests, even though total free memory is sufficient.

**Prevention strategies for our game**:
1. Use arena allocators for level data (bulk free on level change)
2. Use pool allocators for fixed-size game objects (no fragmentation possible)
3. Use frame allocators for temporary data (cleared every frame)
4. Reserve `malloc`/`free` for one-time startup allocations only
5. Pre-allocate maximum capacity at startup; never grow at runtime

## Aligned Allocation

Some data (SIMD vectors, GPU buffers) requires specific memory alignment. The trick: allocate extra bytes, bump the address up to the alignment boundary, and store the adjustment amount in the byte just before the returned address.

```c
void* alloc_aligned(Arena* arena, size_t size, size_t alignment) {
    ASSERT(alignment > 0 && (alignment & (alignment - 1)) == 0); // power of 2
    size_t expanded = size + alignment;
    uintptr_t raw = (uintptr_t)arena_alloc(arena, expanded);
    size_t mask = alignment - 1;
    size_t misalign = raw & mask;
    size_t adjustment = alignment - misalign;
    uintptr_t aligned = raw + adjustment;
    // Store adjustment in the byte before the aligned address
    ((uint8_t*)aligned)[-1] = (uint8_t)adjustment;
    return (void*)aligned;
}
```

## Practical Tips

- **Profile before optimizing**: `malloc` is fine for loading screens and one-time setup. Only optimize the hot path.
- **Static arrays are underrated**: `static Enemy enemies[MAX_ENEMIES]` is cache-friendly, zero-fragmentation, and dead simple.
- **Watch for the 80-20 rule**: A handful of allocation sites usually account for the vast majority of allocations at runtime. Find and fix those first.
