# Rogue & Duel: Opponent-Level Selection

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Change Rogue and Duel effects from "pick a specific card" to "pick an opponent" with random card selection by the server. Add a yellow orbiting border indicator around opponent names during selection.

**Architecture:** Remove `hand_index` from ROGUE_REVEAL/DUEL_PICK commands. Server randomly picks the card and broadcasts the chosen index via new NetPlayerView fields. Client reads these fields from state updates to animate the correct card. New opponent name indicator windows with hit-testing replace card-level hit-testing.

**Tech Stack:** C11, Raylib, existing `draw_border_segment()` for the orbiting gold border.

---

### Task 1: Protocol — Remove hand_index from ROGUE_REVEAL and DUEL_PICK

**Files:**
- Modify: `src/core/input_cmd.h:115-119`
- Modify: `src/net/protocol.h:240-247`
- Modify: `src/net/protocol.c:450-461` (serialization)
- Modify: `src/net/protocol.c:519-535` (deserialization)
- Modify: `src/net/protocol.c:2413-2419` (net_input_cmd_from_local)

- [ ] **Step 1: Remove hand_index from InputCmd rogue_reveal and duel_pick**

In `src/core/input_cmd.h`, change:
```c
/* INPUT_CMD_ROGUE_REVEAL: */
struct { int target_player; int hand_index; } rogue_reveal;

/* INPUT_CMD_DUEL_PICK: */
struct { int target_player; int hand_index; } duel_pick;
```
To:
```c
/* INPUT_CMD_ROGUE_REVEAL: */
struct { int target_player; } rogue_reveal;

/* INPUT_CMD_DUEL_PICK: */
struct { int target_player; } duel_pick;
```

- [ ] **Step 2: Remove hand_index from NetInputCmd rogue_reveal and duel_pick**

In `src/net/protocol.h`, change:
```c
struct {
    int8_t target_player;
    int8_t hand_index;
} rogue_reveal; /* ROGUE_REVEAL */
struct {
    int8_t target_player;
    int8_t hand_index;
} duel_pick; /* DUEL_PICK */
```
To:
```c
struct {
    int8_t target_player;
} rogue_reveal; /* ROGUE_REVEAL */
struct {
    int8_t target_player;
} duel_pick; /* DUEL_PICK */
```

- [ ] **Step 3: Update serialization (protocol.c:450-461)**

Change ROGUE_REVEAL serialization from 2 bytes to 1:
```c
case INPUT_CMD_ROGUE_REVEAL:
    if (len < off + 1)
        return -1;
    write_i8(buf, &off, m->rogue_reveal.target_player);
    break;
case INPUT_CMD_DUEL_PICK:
    if (len < off + 1)
        return -1;
    write_i8(buf, &off, m->duel_pick.target_player);
    break;
```

- [ ] **Step 4: Update deserialization (protocol.c, deser_input_cmd)**

Mirror the serialization changes — read 1 byte for each.

- [ ] **Step 5: Update net_input_cmd_from_local (protocol.c:2413-2419)**

Remove the `hand_index` lines:
```c
case INPUT_CMD_ROGUE_REVEAL:
    out->rogue_reveal.target_player = (int8_t)cmd->rogue_reveal.target_player;
    break;
case INPUT_CMD_DUEL_PICK:
    out->duel_pick.target_player = (int8_t)cmd->duel_pick.target_player;
    break;
```

- [ ] **Step 6: Build and fix all compile errors from removed hand_index**

Run: `make debug-all 2>&1 | grep error`
Fix any remaining references to `.hand_index` in rogue_reveal or duel_pick contexts.

---

### Task 2: Add server-chosen card index fields to NetPlayerView

**Files:**
- Modify: `src/net/protocol.h:596-598` (NetPlayerView)
- Modify: `src/net/protocol.c` (net_build_player_view serialization/deserialization)
- Modify: `src/net/state_recv.c:344-349` (client state apply)
- Modify: `src/phase2/phase2_state.h` (TransmuteRound)

- [ ] **Step 1: Add fields to TransmuteRound in phase2_state.h**

Find the `TransmuteRound` struct and add:
```c
int rogue_chosen_card_idx;  /* server-chosen card for rogue reveal, -1 = none */
int duel_chosen_card_idx;   /* server-chosen card for duel pick, -1 = none */
int duel_chosen_target;     /* server-chosen target player for duel, -1 = none */
```

- [ ] **Step 2: Add fields to NetPlayerView in protocol.h**

After `duel_pending_winner` (line 598), add:
```c
int8_t  rogue_chosen_card_idx;  /* server-chosen card for rogue, -1 = none */
int8_t  duel_chosen_card_idx;   /* server-chosen card for duel, -1 = none */
int8_t  duel_chosen_target;     /* server-chosen target for duel, -1 = none */
```

- [ ] **Step 3: Populate in net_build_player_view (protocol.c:2325-2329)**

After the rogue/duel pending winner lines, add:
```c
out->rogue_chosen_card_idx =
    (int8_t)p2->round.transmute_round.rogue_chosen_card_idx;
out->duel_chosen_card_idx =
    (int8_t)p2->round.transmute_round.duel_chosen_card_idx;
out->duel_chosen_target =
    (int8_t)p2->round.transmute_round.duel_chosen_target;
```

- [ ] **Step 4: Serialize/deserialize the new fields**

Add the 3 new int8_t fields to the NetPlayerView serialization and deserialization functions (follow the pattern of existing int8_t fields like `trick_winner`). Update the `NET_PLAYER_VIEW_MAX_SIZE` constant.

- [ ] **Step 5: Apply in state_recv.c**

In `state_recv_apply()`, after the rogue/duel pending winner section (line ~349), add:
```c
p2->round.transmute_round.rogue_chosen_card_idx =
    (int)view->rogue_chosen_card_idx;
p2->round.transmute_round.duel_chosen_card_idx =
    (int)view->duel_chosen_card_idx;
p2->round.transmute_round.duel_chosen_target =
    (view->duel_chosen_target >= 0)
        ? remap_seat(view->duel_chosen_target, my) : -1;
```

- [ ] **Step 6: Initialize fields to -1**

Ensure these fields are initialized to -1 wherever TransmuteRound is zeroed/initialized. Search for where `rogue_pending_winner` is initialized to -1 and add the new fields alongside.

- [ ] **Step 7: Build**

Run: `make debug-all 2>&1 | grep error`

---

### Task 3: Server — Random card selection for ROGUE_REVEAL and DUEL_PICK

**Files:**
- Modify: `src/server/server_game.c:550-612` (command handlers)

- [ ] **Step 1: Update INPUT_CMD_ROGUE_REVEAL handler**

In `server_game.c`, replace the rogue reveal handler (lines 557-570):
```c
{
    int target = cmd->rogue_reveal.target_player;
    if (target < 0 || target >= NUM_PLAYERS || target == seat) {
        REJECT("Invalid Rogue target");
    }
    if (gs->players[target].hand.count <= 0) {
        REJECT("Target has no cards");
    }

    /* Server randomly picks which card to reveal */
    int hidx = rand() % gs->players[target].hand.count;

    printf("  [Rogue] %s reveals %s's card: %s\n",
           sv_player_name(seat), sv_player_name(target),
           card_name(gs->players[target].hand.cards[hidx]));

    /* Store chosen card so it's broadcast to the client */
    p2->round.transmute_round.rogue_chosen_card_idx = hidx;

    p2->round.transmute_round.rogue_pending_winner = -1;

    /* Check for duel after rogue */
    if (gs->phase == PHASE_PLAYING &&
        p2->round.transmute_round.duel_pending_winner >= 0) {
        int dw = p2->round.transmute_round.duel_pending_winner;
        if (gs->players[dw].is_human) {
            sg->play_substate = SV_PLAY_DUEL_PICK_WAIT;
        } else {
            sv_execute_duel_ai(sg, dw);
            sg->play_substate = SV_PLAY_WAIT_TURN;
        }
    } else {
        sg->play_substate = SV_PLAY_WAIT_TURN;
    }
    sg->state_dirty = true;
}
return true;
```

- [ ] **Step 2: Update INPUT_CMD_DUEL_PICK handler**

Replace the duel pick handler (lines 596-611):
```c
{
    int target = cmd->duel_pick.target_player;
    if (target < 0 || target >= NUM_PLAYERS || target == seat) {
        REJECT("Invalid Duel target");
    }
    if (gs->players[target].hand.count <= 0) {
        REJECT("Target has no cards");
    }

    /* Server randomly picks which card to take */
    int hidx = rand() % gs->players[target].hand.count;

    /* Store target and chosen card for DUEL_GIVE step */
    sg->duel_target_player = target;
    sg->duel_target_hand_index = hidx;

    /* Broadcast chosen card info to client */
    p2->round.transmute_round.duel_chosen_card_idx = hidx;
    p2->round.transmute_round.duel_chosen_target = target;

    sg->play_substate = SV_PLAY_DUEL_GIVE_WAIT;
    sg->state_dirty = true;
}
return true;
```

- [ ] **Step 3: Clear chosen fields after use**

In the DUEL_GIVE handler (after swap), add:
```c
p2->round.transmute_round.duel_chosen_card_idx = -1;
p2->round.transmute_round.duel_chosen_target = -1;
```

In the DUEL_RETURN handler (after clearing), add:
```c
p2->round.transmute_round.duel_chosen_card_idx = -1;
p2->round.transmute_round.duel_chosen_target = -1;
```

Also clear `rogue_chosen_card_idx = -1` after the rogue state transition.

- [ ] **Step 4: Build**

Run: `make debug-all 2>&1 | grep error`

---

### Task 4: Client input — Opponent-level hit testing

**Files:**
- Modify: `src/game/process_input.c:530-614`
- Modify: `src/render/render.h` (add opponent indicator rects + hover state)
- Modify: `src/render/render.c` (compute indicator rects)

- [ ] **Step 1: Add opponent indicator fields to RenderState**

In `src/render/render.h`, near `opponent_hover_active` (line 351), add:
```c
Rectangle opponent_indicator_rects[NUM_PLAYERS]; /* name indicator windows, indexed by local seat */
int       opponent_hover_player;                 /* local seat of hovered opponent, -1 = none */
float     opponent_border_t;                     /* orbiting border timer 0-1 */
```

- [ ] **Step 2: Compute indicator rects in render.c**

In the opponent name drawing function (`draw_phase_playing`, around line 3797), after computing `lx, ly` for each opponent, also compute and store the indicator rect:
```c
int tw = hh_measure_text(rs, player_name(p, rs), name_fs);
float pad = 8.0f * s;
rs->opponent_indicator_rects[p] = (Rectangle){
    lx - pad, ly - pad,
    (float)tw + pad * 2.0f,
    (float)name_fs + pad * 2.0f
};
```
Note: `rs` must be non-const here. If the function takes `const RenderState*`, use a separate computation step in the update path or cast appropriately. Actually, since this data is layout-derived, compute it in the sync/layout path or in `draw_phase_playing` using a mutable pointer (the draw functions already receive mutable `RenderState*`).

- [ ] **Step 3: Update opponent border timer**

In `render_update()` (render.c, near where `draft_wait_border_t` is updated), add:
```c
if (rs->opponent_hover_active) {
    rs->opponent_border_t += dt * 0.4f;
    if (rs->opponent_border_t >= 1.0f)
        rs->opponent_border_t -= 1.0f;
}
```

- [ ] **Step 4: Replace card-level hit test with opponent-level in process_input.c**

Replace the FLOW_ROGUE_CHOOSING handler (lines 589-614):
```c
if (flow_step == FLOW_ROGUE_CHOOSING) {
    /* Update hover state */
    rs->opponent_hover_player = -1;
    for (int p = 1; p < NUM_PLAYERS; p++) {
        if (CheckCollisionPointRec(mouse, rs->opponent_indicator_rects[p])) {
            rs->opponent_hover_player = p;
            break;
        }
    }
    /* Click on hovered opponent */
    if (rs->opponent_hover_player > 0) {
        input_cmd_push((InputCmd){
            .type = INPUT_CMD_ROGUE_REVEAL,
            .source_player = 0,
            .rogue_reveal = { .target_player = rs->opponent_hover_player },
        });
    }
    break;
}
```

Replace the FLOW_DUEL_PICK_OPPONENT handler (lines 532-556):
```c
if (flow_step == FLOW_DUEL_PICK_OPPONENT) {
    /* Update hover state */
    rs->opponent_hover_player = -1;
    for (int p = 1; p < NUM_PLAYERS; p++) {
        if (CheckCollisionPointRec(mouse, rs->opponent_indicator_rects[p])) {
            rs->opponent_hover_player = p;
            break;
        }
    }
    /* Click on hovered opponent */
    if (rs->opponent_hover_player > 0) {
        input_cmd_push((InputCmd){
            .type = INPUT_CMD_DUEL_PICK,
            .source_player = 0,
            .duel_pick = { .target_player = rs->opponent_hover_player },
        });
    }
    break;
}
```

- [ ] **Step 5: Update hover detection for non-click frames**

The hover state also needs to update when the mouse moves without clicking. Move the hover detection out of the `is->pressed` block into the general per-frame section of process_input, or add a separate hover update in `render_update`:
```c
/* Update opponent hover (every frame, not just on click) */
if (rs->opponent_hover_active) {
    rs->opponent_hover_player = -1;
    Vector2 mouse = GetMousePosition();
    for (int p = 1; p < NUM_PLAYERS; p++) {
        if (CheckCollisionPointRec(mouse, rs->opponent_indicator_rects[p])) {
            rs->opponent_hover_player = p;
            break;
        }
    }
}
```

- [ ] **Step 6: Build**

Run: `make debug-all 2>&1 | grep error`

---

### Task 5: Client flow — Wait for server-chosen card index

**Files:**
- Modify: `src/game/turn_flow.c:260-275` (try_start_rogue)
- Modify: `src/game/turn_flow.c:312-327` (try_start_duel)
- Modify: `src/game/turn_flow.c:602-613` (FLOW_ROGUE_CHOOSING)
- Modify: `src/game/turn_flow.c:676-705` (FLOW_DUEL_PICK_OPPONENT)
- Modify: `src/main.c` (inline command routing)

- [ ] **Step 1: Split FLOW_ROGUE_CHOOSING into two substates**

The rogue flow now has two phases:
1. **ROGUE_CHOOSING**: Player picks an opponent (click indicator window). On click, send command to server. Transition to a waiting state.
2. **ROGUE_WAITING**: Wait for server state update carrying `rogue_chosen_card_idx >= 0`. Then launch the card flight animation.

Add `FLOW_ROGUE_WAITING` to the FlowStep enum in `turn_flow.h` (after FLOW_ROGUE_CHOOSING):
```c
FLOW_ROGUE_CHOOSING,
FLOW_ROGUE_WAITING,    /* waiting for server to send chosen card index */
```

- [ ] **Step 2: Update inline routing in main.c**

Change the rogue reveal inline handler to transition to ROGUE_WAITING:
```c
} else if (cmd.type == INPUT_CMD_ROGUE_REVEAL &&
           flow.step == FLOW_ROGUE_CHOOSING) {
    int tp = cmd.rogue_reveal.target_player;
    if (tp > 0 && tp < NUM_PLAYERS && tp != flow.rogue_winner) {
        flow.rogue_reveal_player = tp;
        flow.step = FLOW_ROGUE_WAITING;
        rs.opponent_hover_active = false;
    }
```

- [ ] **Step 3: Add FLOW_ROGUE_WAITING handler in turn_flow.c**

```c
case FLOW_ROGUE_WAITING: {
    /* Wait for server to broadcast the chosen card index */
    int chosen = p2->round.transmute_round.rogue_chosen_card_idx;
    if (chosen >= 0) {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        int rp = flow->rogue_reveal_player;
        flow->rogue_reveal_card_idx = chosen;
        p2->round.transmute_round.rogue_chosen_card_idx = -1;
#ifdef DEBUG
        fprintf(stderr, "[DBG-FLOW] rogue server chose: rp=%d ci=%d\n", rp, chosen);
#endif
        char msg[CHAT_MSG_LEN];
        snprintf(msg, sizeof(msg), "Rogue: You reveal %s's card!",
                 p2_player_name(rp));
        rogue_launch_flight(flow, rs, rp, chosen, msg, anim_m);
    }
    break;
}
```

- [ ] **Step 4: Remove old FLOW_ROGUE_CHOOSING card-index check**

In the existing FLOW_ROGUE_CHOOSING handler, remove the card-index-based launch:
```c
case FLOW_ROGUE_CHOOSING:
    /* Opponent selection handled by process_input + inline routing.
     * Once routing sets rogue_reveal_player and transitions to
     * ROGUE_WAITING, the waiting handler takes over. */
    break;
```

- [ ] **Step 5: Split FLOW_DUEL_PICK_OPPONENT similarly**

Add `FLOW_DUEL_WAITING` to FlowStep enum (after FLOW_DUEL_PICK_OPPONENT):
```c
FLOW_DUEL_PICK_OPPONENT,
FLOW_DUEL_WAITING,     /* waiting for server to send chosen card index */
```

Update the duel pick inline handler in main.c:
```c
} else if (cmd.type == INPUT_CMD_DUEL_PICK &&
           flow.step == FLOW_DUEL_PICK_OPPONENT) {
    int tp = cmd.duel_pick.target_player;
    if (tp > 0 && tp < NUM_PLAYERS && tp != flow.duel_winner) {
        flow.duel_target_player = tp;
        flow.step = FLOW_DUEL_WAITING;
        rs.opponent_hover_active = false;
    }
```

- [ ] **Step 6: Add FLOW_DUEL_WAITING handler**

```c
case FLOW_DUEL_WAITING: {
    int chosen = p2->round.transmute_round.duel_chosen_card_idx;
    int target = flow->duel_target_player;
    if (chosen >= 0 && target > 0) {
        float anim_m = settings_anim_multiplier(settings->anim_speed);
        flow->duel_target_card_idx = chosen;
        p2->round.transmute_round.duel_chosen_card_idx = -1;
        p2->round.transmute_round.duel_chosen_target = -1;
        /* Animate opponent's card to center (existing logic from FLOW_DUEL_PICK_OPPONENT) */
        if (chosen < rs->hand_visual_counts[target]) {
            int cv_idx = rs->hand_visuals[target][chosen];
            if (cv_idx >= 0 && cv_idx < rs->card_count) {
                rs->cards[cv_idx].revealed_to =
                    (uint8_t)((1 << flow->duel_winner) | (1 << target));
                rs->cards[cv_idx].z_order = 200;
                anim_start(&rs->cards[cv_idx],
                           layout_board_center(&rs->layout), 0.0f,
                           ANIM_EFFECT_FLIGHT_DURATION * anim_m,
                           EASE_IN_OUT_QUAD);
                flow->duel_staged_cv_idx = cv_idx;
            }
        }
        flow->step = FLOW_DUEL_ANIM_TO_CENTER;
        flow->timer = ANIM_EFFECT_FLIGHT_DURATION * anim_m;
    }
    break;
}
```

- [ ] **Step 7: Remove old card-index logic from FLOW_DUEL_PICK_OPPONENT**

```c
case FLOW_DUEL_PICK_OPPONENT:
    /* Opponent selection handled by process_input + inline routing.
     * Once routing sets duel_target_player and transitions to
     * DUEL_WAITING, the waiting handler takes over. */
    break;
```

- [ ] **Step 8: Allow state consumption during ROGUE_WAITING and DUEL_WAITING**

In `main.c`, the state defer logic (line ~787) currently only allows consumption during FLOW_IDLE and FLOW_WAITING_FOR_HUMAN. Add the new waiting states:
```c
bool would_defer = (flow.step != FLOW_IDLE &&
                    flow.step != FLOW_WAITING_FOR_HUMAN &&
                    flow.step != FLOW_ROGUE_WAITING &&
                    flow.step != FLOW_DUEL_WAITING);
```

- [ ] **Step 9: Build**

Run: `make debug-all 2>&1 | grep error`

---

### Task 6: Render — Opponent indicator windows with orbiting border

**Files:**
- Modify: `src/render/render.c` (draw_phase_playing, around line 3797)

- [ ] **Step 1: Draw indicator windows during rogue/duel selection**

In `draw_phase_playing()`, after drawing opponent names (line 3818), add indicator rendering when `opponent_hover_active` is true:
```c
/* Opponent selection indicator (Rogue/Duel) */
if (rs->opponent_hover_active) {
    Rectangle ir = rs->opponent_indicator_rects[p];
    bool hovered = (p == rs->opponent_hover_player);

    /* Background panel */
    DrawRectangleRounded(ir, 0.2f, 8,
                         (Color){30, 30, 30, hovered ? 180 : 120});

    /* Orbiting gold border */
    float speed = hovered ? 0.8f : 0.4f;
    float seg_start = fmodf(rs->opponent_border_t * (speed / 0.4f), 1.0f);
    float seg_end = seg_start + 0.20f;
    unsigned char alpha = hovered ? 255 : 120;
    Color glow = (Color){255, 215, 0, alpha};
    draw_border_segment(ir, 0.2f, seg_start, seg_end, 3.0f, glow);
}
```

Note: the name text should be drawn AFTER the background panel so it's on top. Reorder if needed.

- [ ] **Step 2: Clear hover state when indicator deactivates**

In `try_start_rogue` and `try_start_duel` (turn_flow.c), where `opponent_hover_active = true` is set, also initialize:
```c
rs->opponent_hover_player = -1;
rs->opponent_border_t = 0.0f;
```

When the effect ends (ROGUE_ANIM_BACK, DUEL_ANIM_EXCHANGE, DUEL_ANIM_RETURN), ensure `opponent_hover_active = false`.

- [ ] **Step 3: Build and test visually**

Run: `make debug-all 2>&1 | grep error`

---

### Task 7: Clean up — Remove stale debug logs and update update.c

**Files:**
- Modify: `src/game/update.c:218-245` (remove rogue/duel command handlers)
- Modify: `src/main.c` (remove DBG-ROUTE/DBG-ROGUE debug prints)

- [ ] **Step 1: Remove rogue/duel handlers from game_update**

Since rogue/duel commands are now handled inline in the routing block, remove the `INPUT_CMD_ROGUE_REVEAL`, `INPUT_CMD_DUEL_PICK`, `INPUT_CMD_DUEL_GIVE`, and `INPUT_CMD_DUEL_RETURN` branches from `game_update()` in `update.c` (they are dead code now).

- [ ] **Step 2: Remove temporary debug fprintf statements**

Remove all `[DBG-ROGUE]`, `[DBG-DUEL]`, `[DBG-ROUTE]`, `[DBG-FLOW]` debug prints added during the earlier bug investigation from `update.c`, `main.c`, and `turn_flow.c`.

- [ ] **Step 3: Build**

Run: `make debug-all 2>&1 | grep error`

---

### Task 8: Seat remapping for target_player

**Files:**
- Modify: `src/main.c` (inline routing)
- Modify: `src/server/server_game.c` (verify seat interpretation)

- [ ] **Step 1: Remap target_player from local to server seat**

In the inline routing in `main.c`, when handling ROGUE_REVEAL and DUEL_PICK, the `target_player` is in local seat numbering (1-3). The server expects server seat numbering. Add remapping before sending:

The client's server seat is `client_net_seat()`. Local seat L maps to server seat `(L + client_net_seat()) % NUM_PLAYERS`.

```c
/* Remap local seat to server seat before sending */
cmd.rogue_reveal.target_player =
    (cmd.rogue_reveal.target_player + client_net_seat()) % NUM_PLAYERS;
```

Same for duel_pick.

- [ ] **Step 2: Verify server-side validation uses server seats**

The server already validates `target != seat` using server seats. No changes needed on server side.

- [ ] **Step 3: Build and test with non-zero seat**

Run: `make debug-all 2>&1 | grep error`
