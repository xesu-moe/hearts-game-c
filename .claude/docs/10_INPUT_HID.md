# 10 — Input and Human Interface Devices

## Input Abstraction Layer

Raylib handles raw device I/O. Our job is to build an **action mapping layer** on top: map physical inputs to game actions so the game logic never references specific keys.

```c
typedef enum GameAction {
    ACTION_NONE = 0,
    ACTION_MOVE_UP,
    ACTION_MOVE_DOWN,
    ACTION_MOVE_LEFT,
    ACTION_MOVE_RIGHT,
    ACTION_ATTACK,
    ACTION_DODGE,
    ACTION_INTERACT,
    ACTION_INVENTORY,
    ACTION_PAUSE,
    ACTION_DEBUG_TOGGLE,
    ACTION_COUNT
} GameAction;

typedef struct ActionBinding {
    int keyboard_key;   // Raylib KEY_* constant, or -1 for none
    int gamepad_button;  // Raylib GAMEPAD_BUTTON_*, or -1
    int gamepad_axis;    // for analog inputs, or -1
    float axis_threshold; // dead zone threshold for axis input
    bool axis_positive;   // which direction triggers this action
} ActionBinding;

ActionBinding g_bindings[ACTION_COUNT] = {
    [ACTION_MOVE_UP]    = { KEY_W,     GAMEPAD_BUTTON_LEFT_FACE_UP,    -1, 0, false },
    [ACTION_MOVE_DOWN]  = { KEY_S,     GAMEPAD_BUTTON_LEFT_FACE_DOWN,  -1, 0, false },
    [ACTION_MOVE_LEFT]  = { KEY_A,     GAMEPAD_BUTTON_LEFT_FACE_LEFT,  -1, 0, false },
    [ACTION_MOVE_RIGHT] = { KEY_D,     GAMEPAD_BUTTON_LEFT_FACE_RIGHT, -1, 0, false },
    [ACTION_ATTACK]     = { KEY_SPACE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN, -1, 0, false },
    [ACTION_DODGE]      = { KEY_LEFT_SHIFT, GAMEPAD_BUTTON_RIGHT_FACE_LEFT, -1, 0, false },
    [ACTION_INTERACT]   = { KEY_E,     GAMEPAD_BUTTON_RIGHT_FACE_UP,   -1, 0, false },
    [ACTION_INVENTORY]  = { KEY_I,     GAMEPAD_BUTTON_RIGHT_FACE_RIGHT,-1, 0, false },
    [ACTION_PAUSE]      = { KEY_ESCAPE,GAMEPAD_BUTTON_MIDDLE_RIGHT,    -1, 0, false },
};
```

## Button States

Track three states for each action: **held** (currently down), **pressed** (just went down this frame), **released** (just went up this frame).

```c
typedef struct InputState {
    bool held[ACTION_COUNT];
    bool pressed[ACTION_COUNT];
    bool released[ACTION_COUNT];
    Vector2 move_axis; // normalized movement direction from analog stick or WASD
} InputState;

InputState g_input = {0};

void input_system_update(void) {
    // Store previous held state for edge detection
    bool prev_held[ACTION_COUNT];
    memcpy(prev_held, g_input.held, sizeof(prev_held));
    
    // Sample current state
    int gamepad = 0; // first gamepad
    bool has_gamepad = IsGamepadAvailable(gamepad);
    
    for (int a = 0; a < ACTION_COUNT; a++) {
        bool key_down = (g_bindings[a].keyboard_key >= 0) && 
                        IsKeyDown(g_bindings[a].keyboard_key);
        bool pad_down = has_gamepad && (g_bindings[a].gamepad_button >= 0) && 
                        IsGamepadButtonDown(gamepad, g_bindings[a].gamepad_button);
        
        g_input.held[a]     = key_down || pad_down;
        g_input.pressed[a]  = g_input.held[a] && !prev_held[a];
        g_input.released[a] = !g_input.held[a] && prev_held[a];
    }
    
    // Compute movement axis from WASD or analog stick
    g_input.move_axis = (Vector2){0, 0};
    
    if (g_input.held[ACTION_MOVE_RIGHT]) g_input.move_axis.x += 1.0f;
    if (g_input.held[ACTION_MOVE_LEFT])  g_input.move_axis.x -= 1.0f;
    if (g_input.held[ACTION_MOVE_DOWN])  g_input.move_axis.y += 1.0f;
    if (g_input.held[ACTION_MOVE_UP])    g_input.move_axis.y -= 1.0f;
    
    // Override with analog stick if available and not in dead zone
    if (has_gamepad) {
        float lx = GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_X);
        float ly = GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_Y);
        float deadzone = 0.2f;
        if (fabsf(lx) > deadzone || fabsf(ly) > deadzone) {
            g_input.move_axis = (Vector2){ lx, ly };
        }
    }
    
    // Normalize to prevent faster diagonal movement
    float len = sqrtf(g_input.move_axis.x * g_input.move_axis.x + 
                      g_input.move_axis.y * g_input.move_axis.y);
    if (len > 1.0f) {
        g_input.move_axis.x /= len;
        g_input.move_axis.y /= len;
    }
}
```

## Using Input in Game Logic

```c
void player_update(Entity* self, float dt) {
    PlayerData* pd = (PlayerData*)self->type_data;
    
    // Movement
    self->velocity.x = g_input.move_axis.x * pd->speed;
    self->velocity.y = g_input.move_axis.y * pd->speed;
    
    // Attack
    if (g_input.pressed[ACTION_ATTACK]) {
        player_start_attack(self);
    }
    
    // Dodge roll
    if (g_input.pressed[ACTION_DODGE] && pd->dodge_cooldown <= 0.0f) {
        player_start_dodge(self);
    }
    
    // Interact with nearby NPCs/objects
    if (g_input.pressed[ACTION_INTERACT]) {
        Entity* nearest = find_nearest_interactable(self->position, 32.0f);
        if (nearest) interact_with(self, nearest);
    }
}
```

## Analog Stick Dead Zones

Analog sticks never perfectly center at (0,0). Apply a **dead zone** to ignore small drift:

```c
// Radial dead zone (better than per-axis)
Vector2 apply_deadzone(Vector2 raw, float deadzone) {
    float len = sqrtf(raw.x * raw.x + raw.y * raw.y);
    if (len < deadzone) return (Vector2){0, 0};
    // Remap [deadzone, 1.0] → [0, 1.0]
    float scale = (len - deadzone) / (1.0f - deadzone);
    return (Vector2){ raw.x / len * scale, raw.y / len * scale };
}
```

## Input Buffering (for responsive combat)

In action games, buffer recent inputs so the player doesn't need frame-perfect timing:

```c
#define INPUT_BUFFER_WINDOW 0.15f // 150ms

typedef struct BufferedInput {
    GameAction action;
    float      timestamp;
} BufferedInput;

#define MAX_BUFFERED 8
BufferedInput g_buffer[MAX_BUFFERED];
int           g_buffer_count = 0;

void input_buffer_add(GameAction action) {
    if (g_buffer_count < MAX_BUFFERED) {
        g_buffer[g_buffer_count++] = (BufferedInput){
            .action = action, .timestamp = g_game_clock.total_time
        };
    }
}

bool input_buffer_consume(GameAction action) {
    float now = g_game_clock.total_time;
    for (int i = 0; i < g_buffer_count; i++) {
        if (g_buffer[i].action == action && 
            (now - g_buffer[i].timestamp) < INPUT_BUFFER_WINDOW) {
            // Remove consumed input
            g_buffer[i] = g_buffer[--g_buffer_count];
            return true;
        }
    }
    return false;
}
```

## Rebindable Controls

Load key bindings from a config file so the player can customize controls:

```c
void load_bindings(const char* path) {
    // Parse a simple key=value file:
    // move_up=W
    // attack=SPACE
    // etc.
    // Map string names to Raylib KEY_* constants
}
```

## Practical Tips

- **Always normalize movement input**: Without normalization, diagonal movement is ~41% faster (√2) than cardinal movement.
- **Input should be read exactly once per frame**, at the beginning of the update loop. Never read input from within entity update functions — read it once and store the state.
- **Test with both keyboard and gamepad** early. It's easy to accidentally hardcode keyboard-only input.
- **Dead zones are per-gamepad**: Some gamepads need larger dead zones than others. Let the player adjust this.
