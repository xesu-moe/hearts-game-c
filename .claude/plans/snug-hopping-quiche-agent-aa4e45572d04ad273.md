## [AGENT:game-developer] [STATUS:plan-ready] [MODULES:main.c,render.h,render.c]

## Plan: Game Flow Pacing — Turn Flow State Machine

### Summary

Add a TurnFlow state machine to `main.c` that gates AI card plays and trick completions behind timed delays and card-movement animations. This replaces the current behavior where AI plays instantly (one per frame at ~16ms) and tricks resolve with no visual pause, making the game unreadable. The flow machine sequences: AI thinking delay, card play animation, trick display pause, and trick collection animation.

### Docs Consulted
- `04_GAME_LOOP.md` — game loop structure, update ordering, dt handling
- `08_UPDATE_LOOP.md` — batched updates, phased updates, subsystem ordering
- `05_TIME_AND_CLOCKS.md` — timer patterns, clock usage for delays

### Validation of the Proposed Approach

**Is TurnFlow the right pattern?** Yes. The core problem is that game logic (AI plays, trick completion) runs at full speed with no visual pacing. A flow state machine in the update function is the correct solution — it is a sequencer that gates game-state-mutating calls behind timers and animation completion. This is exactly the "game object state machine" pattern from `08_UPDATE_LOOP.md`.

**Key issues with the original plan proposal:**

1. **TurnFlow belongs in `main.c`, not a new file.** The flow machine is small (one enum, one struct, one function) and only touches `main.c`'s `update()`. Creating a separate file for ~80 lines of code that is tightly coupled to `update()` would be over-engineering. Correct decision in the original plan.

2. **`sync_needed` flag on RenderState is the wrong approach.** The original plan proposes adding a `sync_needed` bool to guard `sync_hands()`. The problem is that `sync_hands()` currently does a full destructive rebuild every frame (wipes `card_count` to 0, re-creates all visuals). This is incompatible with animations — if you rebuild all visuals, any in-flight animation state is lost. Instead of a simple bool flag, we need `sync_hands()` to be **skippable when animations are active**. The flow machine itself should control when sync happens: sync after game state changes, skip during animations. The cleanest approach: the flow machine sets a flag `sync_needed = true` when it mutates game state, and `render_update()` only calls `sync_hands()` when that flag is set, then clears it.

3. **Fixed-timestep vs. real-time for flow timers.** The current loop runs `update()` inside a fixed-timestep accumulator (`while (clk.accumulator >= FIXED_DT)`). Flow timers (delays, animation durations) should use **real time (raw_dt)**, not fixed-dt, because they are purely visual pacing. However, `update()` currently receives `FIXED_DT`. Two options:
   - (A) Move flow updates out of the fixed-timestep loop and into per-frame updates alongside `render_update()`. This is cleaner because flow pacing is a presentation concern.
   - (B) Pass `raw_dt` into the flow update separately.

   **Recommendation: Option A.** Move the flow state machine to run once per frame with `raw_dt`, outside the fixed-timestep loop. The fixed-timestep loop should only contain game-logic updates (command processing). This matches the doc guidance: visual/animation systems run per-frame, game logic runs at fixed rate.

4. **The flow machine must handle the human player's turn too.** The original plan only gates AI turns. But after the human plays a card, the card should also animate to the trick area before the next player acts. The flow machine should trigger for ALL card plays, not just AI.

5. **Trick completion needs two phases, not one.** The original plan has FLOW_TRICK_DISPLAY (show complete trick) and FLOW_TRICK_COLLECTING (cards move to winner). This is correct and well-designed.

6. **What happens when it is the human's turn?** The flow machine should enter a FLOW_WAITING_FOR_HUMAN state where it does nothing — just waits for the human to play via input. After the human plays (detected by checking if the trick gained a card), it transitions to FLOW_CARD_ANIMATING.

### New Types

```c
// [file: src/main.c] — all types are file-local (static)

typedef enum FlowStep {
    FLOW_IDLE,              // Decide what to do next
    FLOW_WAITING_FOR_HUMAN, // Human's turn — wait for input to play a card
    FLOW_AI_THINKING,       // Artificial delay before AI plays (0.4s)
    FLOW_CARD_ANIMATING,    // Card sliding from hand to trick area (0.25s)
    FLOW_TRICK_DISPLAY,     // Show completed trick, announce winner (1.0s)
    FLOW_TRICK_COLLECTING,  // Cards slide to winner (0.3s)
    FLOW_BETWEEN_TRICKS,    // Brief pause before next trick starts (0.2s)
} FlowStep;

typedef struct TurnFlow {
    FlowStep step;
    float    timer;           // countdown for current step
    int      animating_player; // player whose card is animating (-1 = none)
    int      trick_winner;    // player who won the trick (set in FLOW_TRICK_DISPLAY)
    bool     sync_needed;     // signal render to rebuild visuals
} TurnFlow;
```

### Timing Constants

```c
// [file: src/main.c]

#define FLOW_AI_THINK_TIME      0.4f   // seconds before AI plays
#define FLOW_CARD_ANIM_TIME     0.25f  // card slide duration (matches ANIM_PLAY_CARD_DURATION in render.c)
#define FLOW_TRICK_DISPLAY_TIME 1.0f   // how long to show completed trick
#define FLOW_TRICK_COLLECT_TIME 0.3f   // cards slide to winner
#define FLOW_BETWEEN_TRICKS_TIME 0.2f  // pause before next trick
```

### New Functions

```c
// [file: src/main.c] — all file-local (static)

// Initialize flow state. Called once at startup and on phase transitions.
static void flow_init(TurnFlow *flow);

// Advance the flow state machine. Called once per frame with raw_dt.
// Reads game state to decide transitions, calls ai_play_card() and
// game_state_complete_trick() at the appropriate moments.
// Sets flow->sync_needed when game state is mutated.
static void flow_update(TurnFlow *flow, GameState *gs, RenderState *rs, float dt);
```

### Flow State Machine Logic (Pseudocode)

```
flow_update(flow, gs, rs, dt):
    // Only active during PHASE_PLAYING
    if gs->phase != PHASE_PLAYING:
        flow->step = FLOW_IDLE
        return

    switch flow->step:

    case FLOW_IDLE:
        // Check if trick is complete (4 cards played)
        if trick_is_complete(&gs->current_trick):
            flow->trick_winner = trick_winner(&gs->current_trick)
            flow->step = FLOW_TRICK_DISPLAY
            flow->timer = FLOW_TRICK_DISPLAY_TIME
            // Set trick_winner_timer in render state for display
            rs->last_trick_winner = flow->trick_winner
            rs->trick_winner_timer = FLOW_TRICK_DISPLAY_TIME
            break

        // Determine whose turn it is
        current = game_state_current_player(gs)
        if current < 0:
            break  // shouldn't happen, defensive

        if current == 0:  // Human
            flow->step = FLOW_WAITING_FOR_HUMAN
        else:  // AI
            flow->step = FLOW_AI_THINKING
            flow->timer = FLOW_AI_THINK_TIME
        break

    case FLOW_WAITING_FOR_HUMAN:
        // Human plays via input commands (handled in update()).
        // After human plays, trick gains a card. Detect this by
        // checking if game_state_current_player changed or trick grew.
        // Actually: the command processing in update() calls game_state_play_card().
        // After that, flow_update runs and sees the trick has one more card.
        // We detect the transition by: if current player changed, human played.
        current = game_state_current_player(gs)
        if current != 0 || trick_is_complete(&gs->current_trick):
            // Human just played — animate their card
            flow->animating_player = 0
            flow->step = FLOW_CARD_ANIMATING
            flow->timer = FLOW_CARD_ANIM_TIME
            flow->sync_needed = true
            // Start card animation in render (see Integration Points)
        break

    case FLOW_AI_THINKING:
        flow->timer -= dt
        if flow->timer <= 0:
            // AI plays its card
            current = game_state_current_player(gs)
            if current > 0:
                ai_play_card(gs, current)
                flow->animating_player = current
                flow->sync_needed = true
            flow->step = FLOW_CARD_ANIMATING
            flow->timer = FLOW_CARD_ANIM_TIME
        break

    case FLOW_CARD_ANIMATING:
        flow->timer -= dt
        if flow->timer <= 0:
            flow->animating_player = -1
            flow->step = FLOW_IDLE  // will re-evaluate: check trick complete or next player
        break

    case FLOW_TRICK_DISPLAY:
        flow->timer -= dt
        if flow->timer <= 0:
            flow->step = FLOW_TRICK_COLLECTING
            flow->timer = FLOW_TRICK_COLLECT_TIME
        break

    case FLOW_TRICK_COLLECTING:
        flow->timer -= dt
        if flow->timer <= 0:
            // NOW actually complete the trick in game state
            game_state_complete_trick(gs)
            flow->sync_needed = true
            flow->step = FLOW_BETWEEN_TRICKS
            flow->timer = FLOW_BETWEEN_TRICKS_TIME
        break

    case FLOW_BETWEEN_TRICKS:
        flow->timer -= dt
        if flow->timer <= 0:
            flow->step = FLOW_IDLE
        break
```

### Integration Points

#### 1. `main.c` — Add TurnFlow as a local variable alongside GameState/RenderState

```c
// In main():
TurnFlow flow;
flow_init(&flow);

// Reset flow on phase transitions (detected in the main loop)
```

#### 2. `main.c` — Remove AI auto-play from `update()`

The current `update()` function (lines 268-285) has this block:

```c
/* AI auto-play during PHASE_PLAYING */
if (gs->phase == PHASE_PLAYING) {
    /* Complete trick if full */
    if (trick_is_complete(&gs->current_trick)) {
        game_state_complete_trick(gs);
    }
    /* AI plays if it's their turn */
    int current = game_state_current_player(gs);
    if (current > 0) { /* AI player */
        ai_play_card(gs, current);
        /* Check if trick completed after AI play */
        if (trick_is_complete(&gs->current_trick)) {
            game_state_complete_trick(gs);
        }
    }
}
```

**Delete this entire block.** The flow state machine now controls when AI plays and when tricks complete.

#### 3. `main.c` — Add flow_update() call in the main loop, OUTSIDE the fixed-timestep loop

Current main loop structure:
```c
while (!WindowShouldClose()) {
    clock_update(&clk);
    process_input(&gs, &rs);

    while (clk.accumulator >= FIXED_DT) {
        update(&gs, &rs, FIXED_DT);       // command processing (fixed rate)
        clk.accumulator -= FIXED_DT;
    }

    render_update(&gs, &rs, clk.raw_dt);  // visual sync (per frame)
    render_draw(&gs, &rs);
}
```

Change to:
```c
while (!WindowShouldClose()) {
    clock_update(&clk);
    process_input(&gs, &rs);

    while (clk.accumulator >= FIXED_DT) {
        update(&gs, &rs, FIXED_DT);       // command processing (fixed rate)
        clk.accumulator -= FIXED_DT;
    }

    flow_update(&flow, &gs, &rs, clk.raw_dt);  // flow pacing (per frame, real time)

    if (flow.sync_needed) {
        flow.sync_needed = false;
        // render_update will do a full sync this frame
    }

    render_update(&gs, &rs, clk.raw_dt);
    render_draw(&gs, &rs);
}
```

#### 4. `main.c` — Reset flow on phase changes

In `update()`, after `game_state_start_game()` succeeds and whenever the phase changes to PHASE_PLAYING, reset the flow:
```c
// After any phase transition that leads to PHASE_PLAYING:
flow_init(&flow);
```

The cleanest way: detect phase change in the main loop (compare `gs.phase` to a `prev_phase` variable) and call `flow_init()` when entering PHASE_PLAYING.

#### 5. `render.c` — Guard sync_hands() with a conditional

Currently `render_update()` calls `sync_hands()` unconditionally every frame (line 229). This destroys any in-flight animation state. Change to:

```c
// In render_update():
// Replace unconditional sync_hands(gs, rs) with:
if (rs->phase_just_changed || rs->sync_needed) {
    // ... (existing save/restore selection logic) ...
    sync_hands(gs, rs);
    // ... (existing restore selection logic) ...
    rs->sync_needed = false;
}
```

This requires adding `sync_needed` to `RenderState`.

#### 6. `render.h` — Add sync_needed field to RenderState

```c
// In struct RenderState, after layout_dirty:
bool sync_needed;     // set by flow machine when game state changes
```

#### 7. `render.c` — Wire up start_animation() for card plays

The `start_animation()` function already exists in `render.c` (lines 55-67) but is marked `__attribute__((unused))`. When the flow machine signals a card play (FLOW_CARD_ANIMATING), the render system should animate the last trick card from its hand position to its trick position.

Add a helper function that the flow machine can trigger:

```c
// [file: render.h] — new public function
// Start animating the most recently played trick card from hand to trick position.
void render_animate_card_play(RenderState *rs, int player_id);
```

```c
// [file: render.c]
void render_animate_card_play(RenderState *rs, int player_id)
{
    // The most recently played card is the last trick visual
    if (rs->trick_visual_count <= 0) return;
    int trick_idx = rs->trick_visuals[rs->trick_visual_count - 1];
    CardVisual *cv = &rs->cards[trick_idx];

    // Calculate source position (center of player's hand area)
    PlayerPosition spos = player_screen_pos(player_id);
    // Use the trick target as the destination (already set by sync)
    // Set start to approximate hand center
    Vector2 hand_center;
    // ... compute from layout based on player position ...

    start_animation(cv, cv->position, ANIM_PLAY_CARD_DURATION, EASE_OUT_QUAD);
    // Actually: start should be hand center, target should be trick position
    cv->start = hand_center;  // where the card comes FROM
    // cv->position will be interpolated by update_animation()
}
```

**However**, this is tricky because sync_hands() has already placed the trick card at its final position. A simpler approach: when `sync_needed` triggers a rebuild, and a card was just played, set that card's start position to a hand-like position and target to the trick position, then mark it animating. This can be done in render_update() by checking a "just played" flag.

**Simpler alternative (recommended):** Instead of a separate `render_animate_card_play()`, have the flow machine store the `animating_player` in the RenderState (or pass it through sync_needed metadata). Then in `render_update()`, after `sync_hands()`, check if there is an animating player and set up the animation for the last trick card.

Add to RenderState:
```c
int   anim_play_player;   // player whose card is animating to trick (-1 = none)
```

In `render_update()`, after sync_hands():
```c
if (rs->anim_play_player >= 0) {
    // Find the last trick visual
    if (rs->trick_visual_count > 0) {
        int trick_idx = rs->trick_visuals[rs->trick_visual_count - 1];
        CardVisual *cv = &rs->cards[trick_idx];

        // Compute hand center for the animating player
        PlayerPosition spos = player_screen_pos(rs->anim_play_player);
        Vector2 hand_center = layout_trick_position(spos, &DEFAULT_LAYOUT);
        // Actually use a position in the hand area, not trick area
        // Approximate: use the name position offset
        Vector2 hand_pos;
        int hand_count = rs->hand_visual_counts[rs->anim_play_player];
        if (hand_count > 0) {
            // Use the middle card position of the hand
            int mid = rs->hand_visuals[rs->anim_play_player][hand_count / 2];
            hand_pos = rs->cards[mid].position;
        } else {
            // Hand is empty, approximate from layout
            Vector2 positions[1];
            int count = 0;
            layout_hand_positions(spos, 1, &DEFAULT_LAYOUT, positions, &count);
            hand_pos = positions[0];
        }

        // Set up animation: from hand to trick position
        cv->start = hand_pos;
        cv->target = cv->position;  // sync_hands already put it at trick pos
        cv->position = hand_pos;    // start from hand
        cv->anim_elapsed = 0.0f;
        cv->anim_duration = ANIM_PLAY_CARD_DURATION;
        cv->anim_ease = EASE_OUT_QUAD;
        cv->animating = true;
    }
    rs->anim_play_player = -1;  // consume the signal
}
```

### Revised RenderState Additions

```c
// [file: render.h] — add to struct RenderState

bool sync_needed;          // set externally when game state changes, triggers sync_hands
int  anim_play_player;     // player whose card should animate to trick (-1 = none)
```

### Revised render_init() Additions

```c
// In render_init():
rs->sync_needed = true;      // initial sync
rs->anim_play_player = -1;
```

### Data Flow Diagram

```
PHASE_PLAYING begins
    -> flow_init(): step = FLOW_IDLE
    -> flow_update() runs each frame:

    FLOW_IDLE
        -> trick complete? -> FLOW_TRICK_DISPLAY (timer = 1.0s)
        -> human's turn?   -> FLOW_WAITING_FOR_HUMAN
        -> AI's turn?      -> FLOW_AI_THINKING (timer = 0.4s)

    FLOW_WAITING_FOR_HUMAN
        -> (input command plays card via update())
        -> detect card was played -> set sync_needed, anim_play_player
        -> FLOW_CARD_ANIMATING (timer = 0.25s)

    FLOW_AI_THINKING
        -> timer expires -> call ai_play_card()
        -> set sync_needed, anim_play_player
        -> FLOW_CARD_ANIMATING (timer = 0.25s)

    FLOW_CARD_ANIMATING
        -> timer expires -> FLOW_IDLE (re-evaluate)

    FLOW_TRICK_DISPLAY
        -> timer expires -> FLOW_TRICK_COLLECTING (timer = 0.3s)

    FLOW_TRICK_COLLECTING
        -> timer expires -> call game_state_complete_trick()
        -> set sync_needed
        -> FLOW_BETWEEN_TRICKS (timer = 0.2s)

    FLOW_BETWEEN_TRICKS
        -> timer expires -> FLOW_IDLE

    (If game_state_complete_trick changes phase to PHASE_SCORING,
     flow_update detects gs->phase != PHASE_PLAYING and resets to FLOW_IDLE)
```

### Files to Create
- None

### Files to Modify
- `src/main.c` — Add FlowStep enum, TurnFlow struct, flow_init(), flow_update(). Remove AI auto-play block from update(). Add flow_update call to main loop. Add phase-change detection to reset flow.
- `src/render.h` — Add `sync_needed` and `anim_play_player` fields to RenderState.
- `src/render.c` — Guard sync_hands() with sync_needed check. Add card play animation setup after sync. Remove `__attribute__((unused))` from start_animation(). Update render_init() for new fields.

### Risks and Mitigations

- **Risk**: Human plays a card during FLOW_WAITING_FOR_HUMAN, but the flow machine does not detect it because `game_state_current_player()` still returns 0 (trick not yet advanced).
  **Mitigation**: Track the trick's `num_played` count before and after `update()`. If it increased, the human played. Store `prev_trick_count` in the flow struct.

- **Risk**: `sync_hands()` is skipped while animations run, causing stale visuals if game state changes without setting sync_needed.
  **Mitigation**: Always set `sync_needed = true` in `flow_update()` whenever game state is mutated. Also set it on phase changes (already handled by `phase_just_changed`).

- **Risk**: The flow machine and `update()` both try to call `game_state_complete_trick()`.
  **Mitigation**: Remove ALL trick completion logic from `update()`. Only the flow machine calls it.

- **Risk**: At round end (13th trick), `game_state_complete_trick()` transitions to PHASE_SCORING. The flow machine must not try to advance further.
  **Mitigation**: The first line of `flow_update()` checks `gs->phase == PHASE_PLAYING`; if not, it resets to FLOW_IDLE and returns.

- **Risk**: During FLOW_WAITING_FOR_HUMAN, the human clicks a card but input is blocked.
  **Mitigation**: Input processing in `process_input()` and command handling in `update()` are independent of the flow machine. The human can always click and play cards. The flow machine simply observes when a card was played and triggers the animation.

### Critical Detail: Detecting Human Card Play

The flow machine needs to know when the human has played a card. The cleanest way:

```c
// In TurnFlow struct:
int prev_trick_count;  // num_played at start of FLOW_WAITING_FOR_HUMAN
```

In FLOW_WAITING_FOR_HUMAN:
```c
if (gs->current_trick.num_played > flow->prev_trick_count) {
    // Human played a card
    flow->animating_player = 0;
    flow->step = FLOW_CARD_ANIMATING;
    flow->timer = FLOW_CARD_ANIM_TIME;
    flow->sync_needed = true;
    rs->anim_play_player = 0;
}
```

When entering FLOW_WAITING_FOR_HUMAN:
```c
flow->prev_trick_count = gs->current_trick.num_played;
```

### Dependency Map Impact
- `render.h` — adds 2 fields to RenderState struct (sync_needed, anim_play_player). All users of RenderState must recompile.
- `render.c` — adds animation setup logic, guards sync_hands().
- `main.c` — adds flow types and logic, removes AI auto-play. No new header dependencies.
- No new files. No new inter-file dependencies.
- **Impact alert**: Any file that includes `render.h` must recompile due to struct change. Currently: `render.c`, `main.c`, `card_render.c`.

### Checklist for Main Agent

- [ ] Add FlowStep enum and TurnFlow struct to `src/main.c` (file-local)
- [ ] Add timing constants (FLOW_AI_THINK_TIME etc.) to `src/main.c`
- [ ] Implement `flow_init()` in `src/main.c`
- [ ] Implement `flow_update()` in `src/main.c` following the state machine pseudocode
- [ ] Delete the "AI auto-play during PHASE_PLAYING" block (lines 268-285) from `update()`
- [ ] Add `TurnFlow flow` variable and `flow_init(&flow)` call in `main()`
- [ ] Add phase-change detection in main loop to reset flow when entering PHASE_PLAYING
- [ ] Add `flow_update()` call in main loop, after fixed-timestep loop, before `render_update()`
- [ ] Pass `flow.sync_needed` to render_update or set `rs.sync_needed` before render_update
- [ ] Add `sync_needed` and `anim_play_player` fields to RenderState in `src/render.h`
- [ ] Update `render_init()` in `src/render.c` to initialize new fields
- [ ] Guard `sync_hands()` in `render_update()` with `phase_just_changed || sync_needed`
- [ ] Add card-play animation setup in `render_update()` after sync, keyed on `anim_play_player`
- [ ] Remove `__attribute__((unused))` from `start_animation()` in `src/render.c`
- [ ] Build with `make` and verify compilation
- [ ] Test: AI turns have visible delay before playing
- [ ] Test: Cards animate from hand to trick area
- [ ] Test: Completed tricks display for ~1 second before collecting
- [ ] Test: Human can still play cards normally during their turn
- [ ] Test: Game transitions to PHASE_SCORING after 13th trick
- [ ] Test: PASS_NONE rounds work correctly (no passing phase)
- [ ] Send updated dependency info to dependency-mapper
- [ ] Request code-auditor review
