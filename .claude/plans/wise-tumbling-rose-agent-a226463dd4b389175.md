## [AGENT:game-developer] [STATUS:plan-ready] [MODULES:input]

## Plan: Input Handling System (Skeleton/Infrastructure)

### Summary

Design a phase-aware input system that cleanly separates raw input polling from game state mutation. The system polls Raylib once per frame in `process_input()`, translates physical inputs into abstract `InputAction` values, and enqueues them into a fixed-size command queue. The `update()` function then drains the queue to drive state transitions. This gives us rebindable controls, a clean input-to-state boundary, and a foundation that AI players can also push commands into.

### Docs Consulted

- `01_ARCHITECTURE_OVERVIEW.md` -- input abstraction sits in the "Low-Level Engine Systems" layer; must be initialized after Raylib but before game state FSM
- `04_GAME_LOOP.md` -- input is read exactly once per frame, before the fixed-timestep update loop; confirmed the existing `process_input()` / `update()` split in main.c
- `08_UPDATE_LOOP.md` -- batched update phases; input read is Phase 0, game logic consumes buffered input in Phase 1
- `09_EVENTS.md` -- event queue pattern with fixed-size ring buffer; informed command queue design (but we use a simpler model since input commands are consumed immediately, not broadcast)
- `10_INPUT_HID.md` -- action mapping layer, button edge detection (pressed/released/held), input buffering; adapted for a card game (no analog sticks, no movement axes)
- **Game Programming Patterns: Command** (gameprogrammingpatterns.com/command.html) -- reified method calls; decoupling input from actor; command queues; undo potential
- **Raylib API (Context7)** -- confirmed signatures: `IsKeyPressed(int key) -> bool`, `IsKeyDown(int key) -> bool`, `IsMouseButtonPressed(int button) -> bool`, `GetMousePosition() -> Vector2`, `GetMouseWheelMove() -> float`

### Design Decisions and Tradeoffs

#### Why Not a Full Command Pattern with Virtual Dispatch?

The Command Pattern (as described in Game Programming Patterns) uses polymorphic command objects with `execute()` methods. In C++ this means a base class with virtual functions; in C it means heap-allocated structs with function pointers. This is the right approach when:

- Commands carry heterogeneous payloads and complex behaviors
- You need undo/redo (each command captures before-state)
- Commands are long-lived, serialized, or networked

For Hollow Hearts, **none of these apply yet**. The game has exactly 6 phases with a small, well-defined set of possible actions per phase. A tagged union of input actions (essentially a "command-lite") gives us:

1. **Decoupling** -- input polling produces action values, update() consumes them. Same separation the Command Pattern provides.
2. **Simplicity** -- no heap allocation, no function pointer indirection, no vtable boilerplate. A `switch` in `update()` handles each action.
3. **Extensibility** -- if we later need undo (unlikely for Hearts) or networking, we can promote the tagged union to full command objects. The queue infrastructure stays the same.
4. **AI compatibility** -- AI players push the same `InputAction` values into the queue, so human and AI input are processed identically.

This is the **"flat over deep"** and **"KISS"** principles from the architecture docs in action.

#### Why a Queue Instead of a Single Action Per Frame?

A card game might seem like it only needs one action per frame. But:

- The human might click a card AND press a confirm button in the same frame
- AI players generate actions programmatically and may produce multiple in one update tick
- A queue naturally handles "no input this frame" (empty queue) without special-casing

The queue is a simple fixed-size ring buffer. 16 slots is generous for a card game. No heap allocation.

#### Phase-Aware Filtering

Not all actions are valid in all phases. Rather than having the input system know about game rules, we use a two-layer approach:

1. **Input layer** (`input.c`): polls devices, produces `InputAction` values, pushes to queue. Knows nothing about game phases.
2. **Game layer** (`game_state.c` / `update()` in `main.c`): drains the queue, validates each action against the current phase, ignores invalid ones.

This keeps the input system reusable and the game rules centralized.

### Prerequisites

- Existing codebase: `main.c` with `process_input()` stub, `game_state.h` with `GamePhase` enum, `card.h` with `Card` type
- Raylib linked and working (confirmed by existing `main.c`)

### New Types

```c
// [file: src/input.h]

#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include "card.h"    // Card type for card-related actions
#include "raylib.h"  // Vector2 for mouse position

// ---- Input Actions ----
// Abstract game actions, decoupled from physical keys/buttons.
// Each action carries a tagged payload for action-specific data.

typedef enum InputActionType {
    INPUT_ACTION_NONE = 0,

    // Universal actions (valid in multiple phases)
    INPUT_ACTION_CONFIRM,       // confirm/proceed (Enter, click confirm button)
    INPUT_ACTION_CANCEL,        // cancel/back (Escape)

    // Card interaction
    INPUT_ACTION_SELECT_CARD,   // click on a card in hand (payload: card_index)
    INPUT_ACTION_DESELECT_CARD, // click on an already-selected card (payload: card_index)
    INPUT_ACTION_PLAY_CARD,     // play the selected/clicked card (payload: card)

    // Menu actions
    INPUT_ACTION_START_GAME,    // start a new game from menu
    INPUT_ACTION_QUIT,          // quit the application

    // Mouse state (raw, for UI hit-testing in render layer)
    INPUT_ACTION_MOUSE_CLICK,   // left-click at position (payload: mouse_pos)

    INPUT_ACTION_COUNT
} InputActionType;

// Tagged union payload for actions that carry data
typedef union InputActionData {
    int   card_index;   // for SELECT_CARD, DESELECT_CARD
    Card  card;         // for PLAY_CARD
    Vector2 mouse_pos;  // for MOUSE_CLICK
} InputActionData;

typedef struct InputAction {
    InputActionType type;
    InputActionData data;
} InputAction;

// ---- Input Command Queue ----
// Fixed-size ring buffer. Produced by process_input(), consumed by update().

#define INPUT_QUEUE_CAPACITY 16

typedef struct InputQueue {
    InputAction actions[INPUT_QUEUE_CAPACITY];
    int         head;   // index of next item to read
    int         tail;   // index of next slot to write
    int         count;  // number of items in queue
} InputQueue;

// ---- Action Bindings ----
// Maps physical keys to abstract actions. One binding per action.
// -1 means "no binding for this device".

typedef struct ActionBinding {
    int keyboard_key;    // Raylib KEY_* constant, or -1
    int mouse_button;    // Raylib MOUSE_BUTTON_*, or -1
} ActionBinding;

// ---- InputState ----
// Per-frame snapshot of which abstract actions are active.
// Edge detection: pressed = just went down, released = just went up.

typedef struct InputState {
    bool held[INPUT_ACTION_COUNT];
    bool pressed[INPUT_ACTION_COUNT];
    bool released[INPUT_ACTION_COUNT];
} InputState;

// ---- Public API ----

// Initialize the input system. Call once at startup, after InitWindow().
void input_init(void);

// Poll all input devices, update edge detection, and push any
// triggered actions into the queue. Call once per frame in process_input().
void input_poll(void);

// Push an action into the queue. Used by input_poll() internally,
// and also by AI to inject actions.
bool input_queue_push(InputAction action);

// Pop the next action from the queue. Returns an action with
// type == INPUT_ACTION_NONE if the queue is empty.
InputAction input_queue_pop(void);

// Peek at the next action without removing it.
InputAction input_queue_peek(void);

// Clear all pending actions from the queue.
void input_queue_clear(void);

// Check if the queue is empty.
bool input_queue_is_empty(void);

// Get the current per-frame input state snapshot (for UI code that
// needs to know "is confirm held?" without consuming queue events).
const InputState *input_get_state(void);

// Get mouse position this frame (convenience wrapper around Raylib).
Vector2 input_get_mouse_pos(void);

// Check if the mouse was clicked this frame (left button pressed).
bool input_mouse_clicked(void);

#endif /* INPUT_H */
```

### New Functions

```c
// [file: src/input.c]

// --- Static globals (file-scoped, not truly global) ---

static InputQueue  s_queue;         // the command queue
static InputState  s_state;         // per-frame edge-detected state
static Vector2     s_mouse_pos;     // cached mouse position this frame
static bool        s_mouse_clicked; // left-click this frame

// --- Queue operations (ring buffer) ---

// input_queue_push(InputAction action)
// Add an action to the tail of the ring buffer.
// Returns false if the queue is full (action is dropped with a warning).

// input_queue_pop(void)
// Remove and return the action at the head.
// Returns INPUT_ACTION_NONE if empty.

// input_queue_peek(void)
// Return the action at the head without removing it.
// Returns INPUT_ACTION_NONE if empty.

// input_queue_clear(void)
// Reset head, tail, count to 0.

// input_queue_is_empty(void)
// Return count == 0.

// --- Initialization ---

// input_init(void)
// Zero-initialize s_queue, s_state, s_mouse_pos.
// No heap allocation. No binding table populated yet
// (bindings will be added in a follow-up plan).

// --- Per-frame polling ---

// input_poll(void)
// 1. Cache mouse position: s_mouse_pos = GetMousePosition()
// 2. Cache mouse click: s_mouse_clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
// 3. If mouse clicked, push INPUT_ACTION_MOUSE_CLICK with mouse_pos payload
// 4. Check keyboard for universally-mapped keys:
//    - IsKeyPressed(KEY_ESCAPE) -> push INPUT_ACTION_CANCEL
//    - IsKeyPressed(KEY_ENTER)  -> push INPUT_ACTION_CONFIRM
//    (Full binding table is a follow-up; this is the skeleton.)
// 5. Update InputState edge detection (pressed/released/held)
//    for all abstract actions based on current queue contents.
//    NOTE: For the skeleton, only CONFIRM and CANCEL get edge detection
//    from keyboard. Card actions come from UI hit-testing (mouse click
//    resolved against card positions), which happens in the game layer.

// --- Accessors ---

// input_get_state(void)
// Return pointer to s_state (read-only).

// input_get_mouse_pos(void)
// Return s_mouse_pos.

// input_mouse_clicked(void)
// Return s_mouse_clicked.
```

### Integration Points

#### 1. `src/main.c` -- `process_input()`

Replace the empty stub with a call to the input system:

```c
static void process_input(GameState *gs)
{
    (void)gs;      // process_input does NOT touch game state
    input_poll();  // poll devices, fill queue
}
```

The `gs` parameter is kept for the signature but intentionally unused -- this enforces the "input does not mutate state" rule documented in the existing comment.

#### 2. `src/main.c` -- `update()`

Drain the input queue and dispatch actions based on the current game phase:

```c
static void update(GameState *gs, float dt)
{
    (void)dt; // card game: most updates are event-driven, not time-driven

    InputAction action;
    while ((action = input_queue_pop()).type != INPUT_ACTION_NONE) {
        switch (gs->phase) {
        case PHASE_MENU:
            // handle INPUT_ACTION_START_GAME, INPUT_ACTION_QUIT
            break;
        case PHASE_PASSING:
            // handle INPUT_ACTION_SELECT_CARD, INPUT_ACTION_CONFIRM
            break;
        case PHASE_PLAYING:
            // handle INPUT_ACTION_PLAY_CARD, INPUT_ACTION_SELECT_CARD
            break;
        case PHASE_SCORING:
            // handle INPUT_ACTION_CONFIRM (advance to next round)
            break;
        case PHASE_GAME_OVER:
            // handle INPUT_ACTION_CONFIRM (return to menu)
            break;
        default:
            break;
        }
    }
}
```

The specific case bodies are **not** part of this skeleton plan. They will be planned per-phase in follow-up plans. The skeleton establishes the dispatch structure.

#### 3. `src/main.c` -- includes and init

Add `#include "input.h"` and call `input_init()` after `SetTargetFPS()`, before the game loop:

```c
#include "input.h"

// ... in main():
SetTargetFPS(TARGET_FPS);
input_init();  // <-- add here, after Raylib init, before game loop
```

#### 4. AI injection point (future)

AI players will call `input_queue_push()` directly to inject actions, making human and AI input indistinguishable from the game logic's perspective. This is not implemented in this skeleton but the API supports it.

### Data Flow Diagram

```
Frame start
    |
    v
clock_update(&clk)           -- measure dt
    |
    v
process_input(&gs)            -- calls input_poll()
    |                             |
    |                             +-> Raylib: GetMousePosition(), IsMouseButtonPressed(),
    |                             |           IsKeyPressed()
    |                             +-> Pushes InputAction values into s_queue ring buffer
    |                             +-> Updates s_state (pressed/held/released)
    |
    v
while (accumulator >= FIXED_DT)
    |
    +-> update(&gs, FIXED_DT)
    |       |
    |       +-> while (input_queue_pop() != NONE)
    |       |       |
    |       |       +-> switch (gs->phase) -> dispatch to phase handler
    |       |       |       |
    |       |       |       +-> calls game_state_*() functions to mutate state
    |       |
    |       +-> (AI logic also runs here in future, pushing to queue first)
    |
    v
render(&gs)                   -- reads InputState for UI hover/highlight
    |                           (does NOT consume queue)
    v
Frame end
```

### Important Design Note: Input Queue and Fixed Timestep

The current game loop runs `update()` potentially multiple times per frame (fixed timestep accumulator). Input is polled once per frame. This means the queue should be drained **only on the first update tick per frame**, or polled outside the fixed-step loop entirely.

**Recommended approach**: Drain the input queue in `process_input()` into a per-frame action list (a simple static array), then have `update()` read from that list. This avoids the queue being empty on the 2nd+ fixed-step iteration.

However, for a card game at 60fps with no physics, the accumulator will almost always run exactly once. The simpler approach (drain in update) works fine in practice. If we later see issues, we promote to the per-frame list approach.

**Decision**: Drain in `update()` for now. Document the caveat in the code.

### Files to Create

- `src/input.h` -- type definitions (InputActionType, InputAction, InputQueue, InputState) and function declarations
- `src/input.c` -- implementation of queue operations, polling, and state management

### Files to Modify

- `src/main.c`:
  - Add `#include "input.h"`
  - Add `input_init()` call in `main()` after `SetTargetFPS()`
  - Replace `process_input()` stub body with `input_poll()` call
  - Replace `update()` stub body with queue-drain + phase dispatch skeleton

### Risks and Mitigations

- **Risk**: Fixed timestep causes queue to be drained on first tick, leaving subsequent ticks input-less.
  **Mitigation**: Documented above. Card game rarely has >1 tick per frame. If it becomes an issue, switch to per-frame action snapshot.

- **Risk**: Queue overflow if AI pushes many actions in one frame.
  **Mitigation**: 16 slots is generous. AI for Hearts makes at most 1 decision per frame. `input_queue_push()` returns false on overflow, caller can handle.

- **Risk**: Mouse click resolves to a card, but the input system does not know card positions (that is rendering's domain).
  **Mitigation**: `INPUT_ACTION_MOUSE_CLICK` carries raw position. The game layer (or a UI hit-test helper) converts mouse position to card index. The input system stays rendering-agnostic.

- **Risk**: Duplicate action types for select vs. play. When does a click become "play" vs. "select"?
  **Mitigation**: At the skeleton level, `MOUSE_CLICK` is the raw event. The phase-specific handler in `update()` interprets it as select or play based on game rules. `SELECT_CARD` and `PLAY_CARD` are higher-level actions that the game layer can push internally or that AI uses directly.

### Dependency Map Impact

- **New**: `input.h` depends on: `card.h` (Card type), `raylib.h` (Vector2)
- **New**: `input.c` depends on: `input.h`, `raylib.h`
- **Modified**: `main.c` gains dependency on: `input.h`
- No circular dependency risk. `input.h` is a low-level engine system; it depends only on leaf types (`card.h`) and the platform layer (`raylib.h`).

[Report these to the dependency-mapper subagent.]

### Checklist for Main Agent

- [ ] Create `src/input.h` with all type definitions and function declarations as specified
- [ ] Create `src/input.c` with:
  - [ ] Ring buffer queue implementation (push, pop, peek, clear, is_empty)
  - [ ] `input_init()` -- zero-initialize all static state
  - [ ] `input_poll()` -- cache mouse state, check keyboard, push raw actions
  - [ ] `input_get_state()`, `input_get_mouse_pos()`, `input_mouse_clicked()` accessors
- [ ] Modify `src/main.c`:
  - [ ] Add `#include "input.h"`
  - [ ] Add `input_init()` after `SetTargetFPS()`
  - [ ] Fill `process_input()` with `input_poll()` call
  - [ ] Fill `update()` with queue-drain loop and phase-based switch skeleton (case bodies are empty/commented stubs)
  - [ ] Add comment in `update()` documenting the fixed-timestep caveat
- [ ] Verify: `make` compiles cleanly with no warnings
- [ ] Verify: game still runs (title screen renders, no crash)
- [ ] Send updated dependency info to `@dependency-mapper`
- [ ] Request `@code-auditor` review of `input.h`, `input.c`, and `main.c` changes
