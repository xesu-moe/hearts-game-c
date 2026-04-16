# 06 — Game Object Model

## Overview

Game objects (also called entities, actors, or agents) are the dynamic elements of the game world: the player, enemies, projectiles, items, triggers, doors, NPCs. The game object model defines how these entities are represented, what data they carry, and how they behave.

## Three Major Architectural Styles

### 1. Object-Centric (C-struct with function pointers) — "Hydro Thunder style"

The simplest approach. Each game object is a C struct containing common fields plus a `void*` user_data pointer for type-specific data, and function pointers for polymorphic behavior.

**This is the model used by the arcade hit Hydro Thunder, which was written entirely in C.**

```c
typedef struct GameObject GameObject;

// Function pointer types for polymorphic behavior
typedef void (*UpdateFn)(GameObject* self, float dt);
typedef void (*RenderFn)(const GameObject* self);
typedef void (*OnEventFn)(GameObject* self, const Event* event);

typedef enum GameObjectType {
    GO_TYPE_NONE = 0,
    GO_TYPE_PLAYER,
    GO_TYPE_ENEMY,
    GO_TYPE_PROJECTILE,
    GO_TYPE_ITEM,
    GO_TYPE_TRIGGER,
    GO_TYPE_NPC,
    GO_TYPE_COUNT
} GameObjectType;

struct GameObject {
    // --- Identity ---
    uint32_t        id;         // unique id
    GameObjectType  type;       // what kind of object
    bool            active;     // is this object alive?
    
    // --- Transform ---
    Vector2         position;
    float           rotation;   // radians
    
    // --- Visual ---
    Texture2D*      texture;
    Rectangle       sprite_rect; // source rectangle in sprite sheet
    
    // --- Collision ---
    Rectangle       collider;   // AABB relative to position
    uint32_t        collision_layer;
    uint32_t        collision_mask;
    
    // --- Type-specific data ---
    void*           user_data;  // points to PlayerData, EnemyData, etc.
    
    // --- Polymorphic behavior ---
    UpdateFn        update;
    RenderFn        render;
    OnEventFn       on_event;
};
```

```c
// Type-specific data examples
typedef struct PlayerData {
    float   health;
    float   max_health;
    float   speed;
    int     xp;
    int     level;
    int     inventory[INVENTORY_SLOTS];
} PlayerData;

typedef struct EnemyData {
    float   health;
    float   max_health;
    float   speed;
    float   attack_damage;
    float   aggro_radius;
    float   attack_cooldown;
    int     ai_state; // IDLE, PATROL, CHASE, ATTACK
    Vector2 patrol_origin;
} EnemyData;
```

```c
// Create a player
GameObject* create_player(Vector2 pos) {
    GameObject* go = game_object_alloc(); // from pool or array
    go->type     = GO_TYPE_PLAYER;
    go->position = pos;
    go->active   = true;
    
    PlayerData* pd = pool_alloc(&player_data_pool);
    pd->health     = 100.0f;
    pd->max_health = 100.0f;
    pd->speed      = 200.0f;
    go->user_data  = pd;
    
    go->update   = player_update;
    go->render   = player_render;
    go->on_event = player_on_event;
    
    return go;
}
```

**Pros**: Very simple, fast, easy to debug, no abstraction overhead.  
**Cons**: user_data requires casting; adding new shared behaviors means adding fields/function pointers to the base struct.

### 2. Component-Based (Composition over Inheritance)

Instead of putting everything in one struct, separate concerns into independent **components**. A game object is a container that owns a set of optional components.

```c
// Component types
typedef struct TransformComponent {
    Vector2 position;
    float   rotation;
    float   scale;
} TransformComponent;

typedef struct SpriteComponent {
    Texture2D* texture;
    Rectangle  src_rect;
    Color      tint;
    int        z_order;
} SpriteComponent;

typedef struct ColliderComponent {
    Rectangle bounds;     // relative to transform
    uint32_t  layer;
    uint32_t  mask;
    bool      is_trigger; // triggers don't block movement
} ColliderComponent;

typedef struct HealthComponent {
    float current;
    float max;
    bool  is_dead;
} HealthComponent;

typedef struct MovementComponent {
    Vector2 velocity;
    float   speed;
    float   friction;
} MovementComponent;

// The game object "hub" — owns its components via pointers
typedef struct Entity {
    uint32_t              id;
    uint32_t              type_tag;  // hashed string or enum
    bool                  active;
    
    // Fixed set of known components (NULL if not present)
    TransformComponent*   transform;
    SpriteComponent*      sprite;
    ColliderComponent*    collider;
    HealthComponent*      health;
    MovementComponent*    movement;
    // Add more component pointers as needed
} Entity;
```

```c
Entity* create_enemy(Vector2 pos) {
    Entity* e = entity_alloc();
    
    e->transform = transform_pool_alloc();
    e->transform->position = pos;
    
    e->sprite = sprite_pool_alloc();
    e->sprite->texture = get_texture("enemy_goblin");
    
    e->collider = collider_pool_alloc();
    e->collider->bounds = (Rectangle){-8, -8, 16, 16};
    
    e->health = health_pool_alloc();
    e->health->current = 50.0f;
    e->health->max = 50.0f;
    
    e->movement = movement_pool_alloc();
    e->movement->speed = 100.0f;
    
    return e;
}
```

**Pros**: Modular, easy to combine capabilities, systems can iterate over specific components.  
**Cons**: More setup code, inter-component communication requires the hub entity.

### 3. Data-Oriented / ECS-lite (Arrays of Components)

Store components in contiguous arrays, indexed by entity ID. Systems iterate over arrays of a specific component type, achieving excellent cache performance.

```c
#define MAX_ENTITIES 4096

// Component arrays (Struct of Arrays layout)
TransformComponent  g_transforms[MAX_ENTITIES];
SpriteComponent     g_sprites[MAX_ENTITIES];
ColliderComponent   g_colliders[MAX_ENTITIES];
HealthComponent     g_health[MAX_ENTITIES];
MovementComponent   g_movement[MAX_ENTITIES];

// Bitfield: which components does each entity have?
typedef enum ComponentFlag {
    COMP_TRANSFORM = 1 << 0,
    COMP_SPRITE    = 1 << 1,
    COMP_COLLIDER  = 1 << 2,
    COMP_HEALTH    = 1 << 3,
    COMP_MOVEMENT  = 1 << 4,
} ComponentFlag;

uint32_t g_entity_flags[MAX_ENTITIES]; // bitmask per entity
bool     g_entity_active[MAX_ENTITIES];
uint32_t g_entity_count = 0;
```

```c
// Systems iterate over entities with matching components
void movement_system_update(float dt) {
    uint32_t required = COMP_TRANSFORM | COMP_MOVEMENT;
    for (uint32_t i = 0; i < g_entity_count; i++) {
        if (!g_entity_active[i]) continue;
        if ((g_entity_flags[i] & required) != required) continue;
        
        TransformComponent* t = &g_transforms[i];
        MovementComponent*  m = &g_movement[i];
        t->position.x += m->velocity.x * dt;
        t->position.y += m->velocity.y * dt;
    }
}

void render_system(void) {
    uint32_t required = COMP_TRANSFORM | COMP_SPRITE;
    for (uint32_t i = 0; i < g_entity_count; i++) {
        if (!g_entity_active[i]) continue;
        if ((g_entity_flags[i] & required) != required) continue;
        
        TransformComponent* t = &g_transforms[i];
        SpriteComponent*    s = &g_sprites[i];
        DrawTexturePro(*s->texture, s->src_rect,
            (Rectangle){t->position.x, t->position.y, s->src_rect.width, s->src_rect.height},
            (Vector2){s->src_rect.width/2, s->src_rect.height/2},
            t->rotation * RAD2DEG, s->tint);
    }
}
```

**Pros**: Cache-friendly, scales well to thousands of entities, systems are independent.  
**Cons**: Indirect entity-to-component lookup, harder to reason about individual entities.

## Recommendation for This Project

Use a **hybrid approach**:
- Use **flat arrays** for entity storage (cache-friendly, simple)
- Use **component flags** to indicate which systems an entity participates in
- Use **type-specific data** (via tagged union or separate pools) for unique behaviors
- Use **function pointers** for polymorphic update/render when the type-specific logic is complex

This combines the simplicity of the Hydro Thunder approach with the modularity of components:

```c
typedef struct Entity {
    uint32_t  id;
    uint16_t  type;
    uint16_t  flags;        // component bitmask
    bool      active;
    
    Vector2   position;
    float     rotation;
    Vector2   velocity;
    
    Rectangle collider;
    float     health;
    float     max_health;
    
    // Type-specific behavior
    UpdateFn  update;
    void*     type_data;    // PlayerData*, EnemyData*, etc.
} Entity;
```

This keeps the hot data (position, velocity, health, active flag) together in one struct for good cache utilization while allowing type-specific logic via function pointers.

## Entity Lifecycle

```
spawn → active → [update every frame] → destroy → recycle slot
```

```c
#define MAX_ENTITIES 2048

Entity   g_entities[MAX_ENTITIES];
uint32_t g_entity_count = 0;
uint32_t g_next_id = 1;

uint32_t entity_spawn(void) {
    for (uint32_t i = 0; i < MAX_ENTITIES; i++) {
        if (!g_entities[i].active) {
            g_entities[i] = (Entity){0};
            g_entities[i].id = g_next_id++;
            g_entities[i].active = true;
            if (i >= g_entity_count) g_entity_count = i + 1;
            return i;
        }
    }
    LOG_ERROR("Entity pool exhausted!");
    return UINT32_MAX;
}

void entity_destroy(uint32_t index) {
    g_entities[index].active = false;
    // Free type_data back to its pool if needed
}
```

## Key Takeaways

1. **Start simple**: The function-pointer-in-struct model works for dozens of entity types. Only switch to a full ECS if you need thousands of entities with shared systems.
2. **Flat arrays over linked lists**: Iterate over a contiguous array, skip inactive slots. This is faster than chasing pointers.
3. **Separate hot and cold data**: Position, velocity, and active flag are accessed every frame. Lore text and inventory are not. Consider splitting them.
4. **ID-based references**: Never store raw pointers to entities. Use IDs and look up the entity from the array. This avoids dangling pointers when entities are destroyed and slots recycled.
