# 09 — Events and Message-Passing

## Why Events?

Games are event-driven. An explosion, a pickup, an enemy spotting the player — these are all events. Without an event system, objects communicate via direct function calls, which creates tight coupling.

**Events decouple senders from receivers.** The explosion doesn't need to know that barrels are flammable, or that the player can be hurt — it just broadcasts an "explosion" event, and interested objects handle it.

## Event Object Design

An event encapsulates: its **type**, its **arguments**, and optionally its **sender**.

```c
// Event types as an enum (simple, fast, good for small/medium games)
typedef enum EventType {
    EVENT_NONE = 0,
    EVENT_DAMAGE,
    EVENT_HEAL,
    EVENT_ENTITY_DIED,
    EVENT_ITEM_PICKED_UP,
    EVENT_DOOR_OPENED,
    EVENT_TRIGGER_ENTER,
    EVENT_TRIGGER_EXIT,
    EVENT_PROJECTILE_HIT,
    EVENT_LEVEL_START,
    EVENT_LEVEL_END,
    EVENT_PLAYER_ATTACK,
    EVENT_ENEMY_AGGRO,
    EVENT_QUEST_UPDATED,
    EVENT_COUNT
} EventType;

// Event argument as a tagged union (variant)
typedef enum EventArgType {
    ARG_NONE,
    ARG_INT,
    ARG_FLOAT,
    ARG_VEC2,
    ARG_ENTITY_ID,
    ARG_STRING_ID,
} EventArgType;

typedef struct EventArg {
    EventArgType type;
    union {
        int32_t  as_int;
        float    as_float;
        Vector2  as_vec2;
        uint32_t as_entity_id;
        uint32_t as_string_id;
    };
} EventArg;

#define MAX_EVENT_ARGS 4

typedef struct Event {
    EventType  type;
    uint32_t   sender_id;      // entity that sent this event (0 = system)
    uint32_t   receiver_id;    // target entity (0 = broadcast)
    float      delivery_time;  // 0 = immediate, >0 = deferred
    int        num_args;
    EventArg   args[MAX_EVENT_ARGS];
} Event;
```

## Building Events with Helper Functions

```c
Event event_damage(uint32_t sender, uint32_t receiver, float amount, Vector2 hit_pos) {
    Event e = {0};
    e.type        = EVENT_DAMAGE;
    e.sender_id   = sender;
    e.receiver_id = receiver;
    e.num_args    = 2;
    e.args[0]     = (EventArg){ .type = ARG_FLOAT, .as_float = amount };
    e.args[1]     = (EventArg){ .type = ARG_VEC2,  .as_vec2  = hit_pos };
    return e;
}

Event event_item_picked_up(uint32_t picker_id, uint32_t item_id) {
    Event e = {0};
    e.type        = EVENT_ITEM_PICKED_UP;
    e.sender_id   = picker_id;
    e.num_args    = 1;
    e.args[0]     = (EventArg){ .type = ARG_ENTITY_ID, .as_entity_id = item_id };
    return e;
}
```

## Event Handlers

Each entity type implements an event handler function:

```c
// In the Entity struct:
typedef void (*OnEventFn)(Entity* self, const Event* event);

// Player's event handler
void player_on_event(Entity* self, const Event* event) {
    PlayerData* pd = (PlayerData*)self->type_data;
    
    switch (event->type) {
    case EVENT_DAMAGE: {
        float damage = event->args[0].as_float;
        pd->health -= damage;
        if (pd->health <= 0.0f) {
            pd->health = 0.0f;
            // Send death event
            event_send(event_entity_died(self->id));
        }
        // Screen shake, hurt sound, flash red, etc.
        break;
    }
    case EVENT_HEAL: {
        float amount = event->args[0].as_float;
        pd->health = fminf(pd->health + amount, pd->max_health);
        break;
    }
    case EVENT_ITEM_PICKED_UP:
        // Add to inventory...
        break;
    default:
        break; // Ignore events we don't care about
    }
}
```

## Event Queue

Events can be sent immediately or queued for later delivery. A queue provides:
- **Control over timing**: Handle events at a safe point in the game loop
- **Future posting**: "Play this sound in 0.5 seconds"
- **Priority**: Handle damage events before cosmetic events

```c
#define MAX_QUEUED_EVENTS 256

typedef struct EventQueue {
    Event  events[MAX_QUEUED_EVENTS];
    int    count;
} EventQueue;

EventQueue g_event_queue = {0};

// Send an event (queued for end-of-frame dispatch)
void event_send(Event event) {
    if (event.delivery_time <= 0.0f) {
        // Queue for this frame's dispatch
        event.delivery_time = 0.0f;
    }
    if (g_event_queue.count < MAX_QUEUED_EVENTS) {
        g_event_queue.events[g_event_queue.count++] = event;
    } else {
        LOG_WARN("Event queue full! Dropping event type %d", event.type);
    }
}

// Send an event to be delivered in the future
void event_send_delayed(Event event, float delay_seconds) {
    event.delivery_time = g_game_clock.total_time + delay_seconds;
    event_send(event);
}

// Dispatch all events whose delivery time has passed
void event_system_dispatch(void) {
    float now = g_game_clock.total_time;
    int remaining = 0;
    
    for (int i = 0; i < g_event_queue.count; i++) {
        Event* e = &g_event_queue.events[i];
        
        if (e->delivery_time > now) {
            // Not yet — keep in queue
            g_event_queue.events[remaining++] = *e;
            continue;
        }
        
        // Dispatch to target
        if (e->receiver_id != 0) {
            // Targeted event — send to specific entity
            Entity* receiver = entity_get_by_id(e->receiver_id);
            if (receiver && receiver->active && receiver->on_event) {
                receiver->on_event(receiver, e);
            }
        } else {
            // Broadcast — send to all entities
            for (uint32_t j = 0; j < g_entity_count; j++) {
                Entity* ent = &g_entities[j];
                if (ent->active && ent->on_event) {
                    ent->on_event(ent, e);
                }
            }
        }
    }
    
    g_event_queue.count = remaining;
}
```

## Interest Registration (Optional Optimization)

To avoid calling every entity's handler for every event, let entities register interest:

```c
#define MAX_LISTENERS 64

typedef struct EventListenerList {
    uint32_t entity_ids[MAX_LISTENERS];
    int      count;
} EventListenerList;

EventListenerList g_listeners[EVENT_COUNT] = {0};

void event_register(EventType type, uint32_t entity_id) {
    EventListenerList* list = &g_listeners[type];
    if (list->count < MAX_LISTENERS) {
        list->entity_ids[list->count++] = entity_id;
    }
}

void event_unregister(EventType type, uint32_t entity_id) {
    EventListenerList* list = &g_listeners[type];
    for (int i = 0; i < list->count; i++) {
        if (list->entity_ids[i] == entity_id) {
            list->entity_ids[i] = list->entity_ids[--list->count];
            return;
        }
    }
}
```

Then dispatch only to registered listeners for that event type.

## Chain of Responsibility

Events can be forwarded along a chain: vehicle → passenger → weapon. Each handler returns `true` if it consumed the event (stop forwarding) or `false` to pass it on.

```c
bool player_on_event(Entity* self, const Event* event) {
    switch (event->type) {
    case EVENT_DAMAGE:
        // Check if shield absorbs it
        if (has_shield(self)) {
            absorb_damage(self, event);
            return true; // consumed — don't forward
        }
        apply_damage(self, event);
        return false; // allow forwarding to equipment, etc.
    default:
        return false; // didn't handle it
    }
}
```

## Practical Tips

1. **Start with immediate dispatch**: Only add queuing when you need deferred events or future posting. Immediate dispatch is simpler to debug.
2. **Event structs on the stack**: For immediate events, allocate the Event struct on the call stack — zero allocation cost.
3. **Queued events must be deep-copied**: When queuing an event, copy the entire struct (args included) into the queue. Don't hold pointers to stack-allocated events.
4. **Use events for loose coupling, not for everything**: Direct function calls are fine when two systems are naturally tightly coupled. Events shine when the sender shouldn't know the receiver's type.
5. **Keep the event type enum small and well-documented**: Every event type should have clear documentation of its arguments.
