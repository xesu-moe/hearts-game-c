# 08 — Update Loop: Ordering, Batching, and Dependencies

## The Naive Approach (and Why It Fails)

The simplest idea — call `entity_update(dt)` on each entity, and have that function update animation, physics, collision, and rendering — breaks down because:

1. **Performance**: Engine subsystems (collision, rendering) are far more efficient when updated in batches, not interleaved per-entity.
2. **Dependencies**: Entity B (weapon in hand) depends on Entity A (character holding it). A must finish updating before B starts.
3. **Inconsistency**: Halfway through the update loop, some entities are at time `t2` and others are still at `t1`.

## Batched Updates (The Right Way)

Each engine subsystem maintains its own internal data and is updated as a batch. Game objects control subsystem state via properties, but don't directly invoke subsystem updates.

```c
// CORRECT game loop structure:
while (!WindowShouldClose()) {
    float dt = get_game_dt();
    
    // Phase 1: Game object logic (AI, player input, state machines)
    for (uint32_t i = 0; i < g_entity_count; i++) {
        if (!g_entities[i].active) continue;
        if (g_entities[i].update) {
            g_entities[i].update(&g_entities[i], dt);
        }
    }
    
    // Phase 2: Movement system (apply velocities)
    movement_system_update(dt);
    
    // Phase 3: Collision detection & resolution (all at once)
    collision_system_update();
    
    // Phase 4: Animation system (advance all sprite animations)
    animation_system_update(dt);
    
    // Phase 5: Camera (follows player — after player has moved)
    camera_system_update(dt);
    
    // Phase 6: Particles / VFX
    particle_system_update(dt);
    
    // Phase 7: Render everything
    BeginDrawing();
    render_system_draw();
    EndDrawing();
    
    // Phase 8: Dispatch deferred events
    event_system_dispatch();
    
    // Phase 9: Cleanup
    destroy_pending_entities();
    frame_alloc_reset();
}
```

### Why batching wins:

- **Cache coherency**: All positions are processed together, then all colliders together, etc. The CPU cache stays warm.
- **No redundant work**: Global calculations (visibility, spatial grid rebuild) happen once per frame, not once per entity.
- **Subsystem independence**: Collision system doesn't need to know about animation; each system only touches its own data.

## Phased Updates

Some entities need to hook into multiple points during the frame. For example:

1. **Pre-animation**: Set animation state based on AI decisions
2. **Post-animation**: Adjust sprite based on animation results
3. **Post-collision**: React to collision events (take damage, bounce)

Implement this with multiple update callbacks:

```c
typedef void (*PreUpdateFn)(Entity* self, float dt);
typedef void (*PostPhysicsFn)(Entity* self, float dt);
typedef void (*LateUpdateFn)(Entity* self, float dt);

// In the game loop:
// Phase 1: Pre-update (AI, input, state changes)
for_each_active_entity(call pre_update);

// Phase 2-4: Systems run...
movement_system_update(dt);
collision_system_update();
animation_system_update(dt);

// Phase 5: Post-physics (react to collisions)
for_each_active_entity(call post_physics);

// Phase 6: Late update (camera, attachment updates)
for_each_active_entity(call late_update);
```

**Optimization**: Maintain separate lists for each phase. If an entity doesn't need `post_physics`, it's not in that list, so we skip iterating over it.

## Bucketed Updates (Inter-Object Dependencies)

When object B depends on object A (e.g., a weapon attached to a character), A must be fully updated before B starts. Group entities into **buckets** by dependency depth:

```
Bucket 0 (roots):    Platforms, vehicles, environmental movers
Bucket 1 (riders):   Characters standing on platforms
Bucket 2 (attached): Weapons held by characters, particles on characters
```

Update each bucket completely (all phases) before starting the next:

```c
typedef enum UpdateBucket {
    BUCKET_ENVIRONMENT = 0,  // platforms, movers
    BUCKET_CHARACTERS,       // player, enemies, NPCs
    BUCKET_ATTACHED,         // weapons, shields, visual attachments
    BUCKET_COUNT
} UpdateBucket;

// Each entity has a bucket assignment
Entity g_entities[MAX_ENTITIES];
// g_entities[i].bucket = BUCKET_CHARACTERS;

void update_bucket(UpdateBucket bucket, float dt) {
    // Pre-update
    for (uint32_t i = 0; i < g_entity_count; i++) {
        if (!g_entities[i].active || g_entities[i].bucket != bucket) continue;
        if (g_entities[i].update) g_entities[i].update(&g_entities[i], dt);
    }
    
    // Movement (only for entities in this bucket)
    movement_system_update_bucket(bucket, dt);
    
    // Collision (only for entities in this bucket)
    collision_system_update_bucket(bucket);
}

// Main loop
void game_update(float dt) {
    for (int b = 0; b < BUCKET_COUNT; b++) {
        update_bucket(b, dt);
    }
    // Then render all entities together
}
```

For most top-down RPGs, 2-3 buckets suffice.

## The One-Frame-Off Bug

**The problem**: During the update loop, entity A has been updated to time `t2`, but entity B still holds state from `t1`. If B reads A's position, it gets the new value. If A reads B's position, it gets the old value. This inconsistency causes jitter or desynchronization.

**Mitigations**:
1. **Bucketed updates**: Entities only read from previously-updated buckets (which are fully at `t2`) or the current bucket (which is still at `t1`).
2. **State caching**: Store both previous and current state. Entities always read from the "previous" state (which is consistent).
3. **Time-stamping**: Tag each entity's state with its current time. Assert that you're reading a state at the expected time.

**For our RPG**: The simplest approach is sufficient — just be careful about the update order within the game loop. Camera reads player position after player has moved. Enemies read player position — this is always fine because player and enemies are in the same bucket and the player's position was set during input processing.

## Deferred Entity Destruction

Never destroy an entity during the update loop — it invalidates the array you're iterating over. Instead, mark it for destruction and clean up at the end of the frame:

```c
#define MAX_PENDING_DESTROYS 64
uint32_t g_pending_destroys[MAX_PENDING_DESTROYS];
int      g_pending_destroy_count = 0;

void entity_mark_destroy(uint32_t entity_index) {
    if (g_pending_destroy_count < MAX_PENDING_DESTROYS) {
        g_pending_destroys[g_pending_destroy_count++] = entity_index;
    }
}

void destroy_pending_entities(void) {
    for (int i = 0; i < g_pending_destroy_count; i++) {
        entity_destroy(g_pending_destroys[i]);
    }
    g_pending_destroy_count = 0;
}
```

## Deferred Entity Spawning

Same principle — don't spawn new entities mid-update. Queue them and create at end of frame or start of next frame.
