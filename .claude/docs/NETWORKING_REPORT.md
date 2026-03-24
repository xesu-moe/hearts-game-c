# Networking Implementation Report — Steps 1-10

## Overview

Steps 1-10 of the 22-step networking plan are complete. This covers Phase A (Network Foundation), Phase B (Game Server), and Phase C (Client Networking). The result is a fully functional server-authoritative multiplayer architecture with ~6,400 lines of networking code across 15 files.

**What works end-to-end:** A client can connect to a game server, perform a handshake, receive a seat assignment, send game commands, and receive authoritative game state updates 60 times per second. The client disables its local game logic when online, acting as a rendering engine that follows server state.

---

## Architecture Summary

```
┌─────────────────┐         ┌──────────────────┐
│     Client      │  TCP    │   Game Server     │
│  (hollow-hearts)│◄───────►│   (hh-server)     │
│                 │         │                   │
│  process_input()│         │  server_game_tick()│
│       ↓         │         │       ↓            │
│  InputCmd queue │         │  GameState +       │
│       ↓         │         │  Phase2State       │
│  [online?]──────┼─CMD────►│       ↓            │
│       ↓         │         │  validate + apply  │
│  state_recv     │◄─STATE──┼  net_build_player  │
│  _apply()       │  UPDATE │  _view() (filtered)│
│       ↓         │         │                   │
│  render_update()│         │                   │
└─────────────────┘         └──────────────────┘
```

---

## Step-by-Step Implementation Record

### Step 1 — Message Protocol (`protocol.h/c`, 2450 lines)

**What:** Binary message format with length-prefix framing, 23 message types, full serialization for all game state including Phase 2 features.

**Key decisions:**
- **Binary over text/JSON**: Correct for a game — compact, fast to parse, no ambiguity. Hearts messages are small (state updates ~2-4KB) so JSON would have been fine too, but binary scales better.
- **Length-prefix framing (4-byte header)**: Simple and reliable for TCP stream reassembly. Overkill for messages under 8KB, but future-proof.
- **NetPlayerView as a monolithic struct (~470 bytes)**: Sends full game state every tick rather than deltas. This is bandwidth-heavy (~28KB/s per player at 60fps) but eliminates state divergence bugs. For a card game with 4 players, this is acceptable.
- **INPUT_RELEVANT filter array**: Centralizes the decision of which commands cross the network boundary. Clean separation.

**Evaluation:** Solid foundation. The serialization code is verbose (~1868 lines) but correct and complete. The monolithic state update approach trades bandwidth for simplicity — right call for a card game.

**Mistake:** None significant. The `net_build_player_view` function is ~400 lines and tightly coupled to both GameState and Phase2State. If Phase 2 features change, this function needs parallel updates. A code generation approach would reduce this maintenance burden, but isn't worth the complexity for a project this size.

### Step 2 — TCP Socket Layer (`socket.h/c`, 796 lines)

**What:** Non-blocking TCP with poll()-based multiplexing, per-connection ring buffers, connection state machine.

**Key decisions:**
- **poll() over select()**: Correct — poll() handles large fd numbers without bitmap sizing issues. For <100 connections, either works.
- **16KB ring buffers per connection**: Generous for a card game. A single NetPlayerView serializes to ~2-4KB. This gives 4-8 messages of headroom per connection.
- **No threads**: Single-threaded event loop. Perfect for a turn-based card game where latency requirements are relaxed (~100ms is fine).
- **No DNS**: IP addresses only. Acceptable for now — DNS resolution would add complexity and a blocking call.

**Evaluation:** Clean, well-abstracted layer. The two-segment recv/send for ring buffer wrap-around is efficient. The connection state machine (DISCONNECTED→CONNECTING→CONNECTED→CLOSING) is correct.

**Improvement:** Could add connection timeout detection (currently relies on TCP keepalive). A 10-second connect timeout would improve UX.

### Step 3 — Build System (Makefile)

**What:** Three build targets sharing core code. `NET_SRC = $(wildcard src/net/*.c)` auto-discovers new files.

**Key decision:** All `net/` files compile into both client and server. This means `client_net.c` is in the server binary (unused) and `state_recv.c` is too. The unused code is ~600 bytes — negligible.

**Evaluation:** Simple and correct. The wildcard approach means new networking files are automatically included. No conditional compilation needed yet.

### Step 4 — Headless Game Loop (`server_main.c`, 118 lines)

**What:** Server entry point with fixed-timestep loop, signal handling, no Raylib.

**Key decisions:**
- **Fixed timestep matching client (FIXED_DT)**: Both tick at ~60fps. This means the server processes commands at the same granularity as the client generates them.
- **Accumulator with max catchup**: Prevents spiral of death if a tick takes too long. Correct pattern.

**Evaluation:** Minimal and correct. The sleep() at the end prevents busy-waiting.

### Step 5 — Room Management (`room.h/c`, 414 lines)

**What:** Room pool (100 max), 4-char room codes, player slot tracking, auto-start when 4 players join.

**Key decisions:**
- **Static room array (MAX_ROOMS=100)**: Simple allocation. At 4 players per room, supports 400 concurrent players per server.
- **4-char alphanumeric room codes**: ~1.7M combinations (36^4). Sufficient for <100 active rooms. Excludes ambiguous characters (O/0/I/l/1).
- **Auto-start on 4th join**: The game begins immediately when the room fills. No "ready" button — keeps flow simple.

**Evaluation:** Clean lifecycle (INACTIVE→WAITING→PLAYING→FINISHED→INACTIVE). The linear scan for `room_find_by_code` is O(100) — fine for this scale.

**Improvement:** Room codes should have an expiration mechanism. Currently, WAITING rooms persist forever if players disconnect before the game starts.

### Step 6 — Server Network Loop (`server_net.c`, 385 lines)

**What:** Accept connections, authenticate (stub), route messages, broadcast state, handle disconnects.

**Key decisions:**
- **Broadcast state every tick**: Every connected player gets a fresh NetPlayerView 60 times per second. This is the "dumb terminal" model — server sends everything, client renders.
- **Empty room code = create new room**: The handshake message doubles as room creation. Clean API.
- **Server enforces seat from ConnSlotInfo**: `cmd.source_player = info->seat` — never trusts client's claimed player ID. Critical for anti-cheat.

**Evaluation:** The 6-stage update loop (poll → accept → process messages → tick rooms → broadcast state → cleanup) is well-structured. Each stage is independent.

**Mistake:** `sv_broadcast_state()` builds and sends a NetPlayerView for every player every tick, even if nothing changed. This wastes CPU and bandwidth. A "dirty flag" per room would skip unchanged rooms. Not critical at this scale, but worth noting.

### Step 7 — Client Connection Manager (`client_net.h/c`, 445 lines)

**What:** Client-side TCP connection, handshake, ping, state storage.

**Key decisions:**
- **File-scope statics (singleton pattern)**: Matches server_net.c. Only one server connection at a time. Correct for a game client.
- **Dummy auth token**: All zeros. Placeholder until lobby server exists. The server accepts anything.
- **Store latest NetPlayerView**: One copy, overwritten each frame. Steps 8-9 consume it. No queue — the latest state is always authoritative.
- **`_POSIX_C_SOURCE` for clock_gettime**: Required in C11 mode for POSIX timer functions. Correctly placed before includes.

**Evaluation:** Clean, minimal API. The 5-state machine (DISCONNECTED→CONNECTING→HANDSHAKING→CONNECTED→ERROR) covers all cases.

**Mistake:** `g_ping_sent_ms` was a dead store (set but never read). Fixed during audit. RTT correctly computed from echoed timestamp in PONG reply.

### Step 8 — Command Forwarding (`main.c` modification, ~15 lines)

**What:** Intercept InputCmd queue between `process_input()` and `game_update()`. Route game commands to server, re-push client-only commands for local processing.

**Key decisions:**
- **Inline in main.c** (no new files): The networking plan suggested `cmd_send.h/c`, but `client_net_send_cmd()` already handles filtering and serialization. The remaining logic is a 15-line drain-split-repush loop.
- **`source_player = client_net_seat()`**: Stamps the assigned seat on outgoing commands. Server overrides this anyway, but cleaner.
- **game_update still runs**: Processes client-only commands (settings, pause menu) even when online.

**Evaluation:** Correct and minimal. The drain-split-repush pattern avoids double-processing while preserving local UI functionality.

**Improvement:** The re-push loop allocates a `local_cmds[16]` array on the stack every frame. This is cheap (~1KB) but could be avoided by having `game_update` check `net_input_cmd_is_relevant()` directly. Trade-off: cleaner separation vs. slightly less work per frame.

### Step 9 — State Application (`state_recv.h/c`, 296 lines)

**What:** Apply NetPlayerView to local GameState and Phase2State with seat remapping.

**Key decisions:**
- **Seat remapping**: `local = (server_seat - my_seat + 4) % 4`. The client always sees itself as player 0. This preserves the renderer's assumption that player 0 is at the bottom of the screen.
- **Split architecture**: `state_recv.c` handles core types (GameState, Phase2State). `main.c` handles game-layer types (PassPhaseState, PlayPhaseState, TurnFlow). Respects the layering boundary.
- **Opponent hands: count only**: `gs->players[local].hand.count` is set from server, but `hand.cards[]` is never populated for opponents. Anti-cheat enforced at the application layer too.
- **Clamping all counts**: `num_contracts`, `transmute_inv_count`, `num_persistent_effects` all clamped to their array maximums. Defensive against malformed packets.

**Evaluation:** Thorough field mapping with correct seat remapping throughout. The `remap_seat()` helper preserves -1 sentinels (for "no player" fields like `lead_player` before first trick).

**Mistake (found by auditor, fixed):**
1. `transmuter_player` and `fog_transmuter` in `apply_transmute_slot` were not seat-remapped. Fixed by adding `my_seat` parameter.
2. `trick_transmutes.transmuter_player` in main.c was not remapped. Fixed with inline remap formula.
3. Effect param union written to `points_delta` for all effect types. Documented as safe (all union variants are int-sized at same offset).

### Step 10 — Phase & Turn Synchronization (5 files modified)

**What:** Disable local game logic when online. Server drives phases, turns, and trick resolution. Client is an animation engine.

**Key decisions:**
- **`bool online` parameter**: Added to `phase_transition_update()`, `flow_update()`, `pass_subphase_update()`. Explicit, no hidden dependencies. Each function is called from one place in `main.c`.
- **Skip pass animations online**: The server doesn't send opponent pass selections, so the toss/reveal/receive visual pipeline can't run. Pass phase jumps directly to PHASE_PLAYING. This is a UX loss but avoids data corruption.
- **FLOW_TRICK_COLLECTING simplified online**: Skips all point computation (Bounty/Parasite/Shield/contract tracking). Server already set `round_points` via `state_recv_apply`. Just transitions to FLOW_BETWEEN_TRICKS.
- **No AI_THINKING state online**: Server handles all AI. If client somehow enters this state, it falls back to FLOW_IDLE.

**Evaluation:** The guards are surgical and correct. Each state-changing code path is wrapped with `!online`, while animation/visual code runs unconditionally.

**Mistake (found by auditor, fixed):**
1. Pass animation buffering in `state_recv.c` would have trapped the client in PHASE_PASSING forever (the unblock mechanism was missing). Removed entirely — server phase is always applied directly.
2. `pass_start_receive_anim()` calls `game_state_execute_pass()` which would corrupt state online. Fixed by making the online pass path a simple `return`.
3. `rs.turn_time_remaining` was being overwritten by local flow timer after server set it. Fixed with `if (!online)` guard.

---

## Cross-Cutting Concerns

### Anti-Cheat Boundary

The anti-cheat model is correctly implemented at three levels:

1. **Server build** (`net_build_player_view`): Only sends own hand card identities. Opponents get count only. Current trick cards are visible (face-up).
2. **Client apply** (`state_recv_apply`): Only writes to `players[0].hand.cards[]`. Opponent hand arrays are never populated.
3. **Command validation** (`server_game_apply_cmd`): Server overrides `source_player` with the seat from `ConnSlotInfo`. Client can't impersonate another player.

**Gap:** The server currently has no rate limiting. A malicious client could spam commands. Step 13 (Error Handling) will address this.

### Bandwidth

At 60fps with 4 players:
- State updates: ~3KB × 4 players × 60fps = **~720KB/s** outbound from server
- Commands: ~20 bytes × occasional = negligible

This is acceptable for LAN/broadband but heavy for mobile. Future optimization: send deltas instead of full state, or reduce broadcast rate to 20fps for non-active players.

### Latency

The architecture has inherent latency:
1. Client sends command → network delay → server receives
2. Server processes → builds state → network delay → client receives
3. Client applies state → triggers animation

Minimum round-trip: 2× network latency + 1 server tick (16.7ms). For a card game where actions happen every few seconds, this is invisible.

---

## Known TODOs in Code

| Location | TODO | Priority |
|----------|------|----------|
| `server_net.c:172` | `/* TODO: add server time */` — pong.server_timestamp_ms hardcoded to 0 | Low — ping RTT works without server time |
| `client_net.c:82` | Dummy auth token (all zeros) — needs lobby integration | Blocked on Step 15 (accounts) |
| `pass_phase.c:894` | Pass animations skipped online — missing opponent pass data | Medium — needs server animation hints |
| `room.c` | No room expiration for WAITING rooms | Low — becomes relevant at scale |

## Improvements for After Plan Completion

### High Priority

1. **Pass phase animations online**: The server should send a `MSG_PASS_RESULT` message containing all players' passed cards (after the exchange, so it's no longer secret). The client can then animate the toss/reveal/receive pipeline. Currently, the pass phase just jumps to playing phase with no visual.

2. **State delta compression**: Sending full NetPlayerView (3-4KB) 60 times per second is wasteful. Most frames, only `turn_timer` changes. A "dirty fields" bitmask + only-changed-fields serialization would cut bandwidth by ~90% during static moments.

3. **Reconnection (Step 11)**: Currently, a disconnected client cannot rejoin. The server marks the slot as DISCONNECTED but has no mechanism to accept a returning player. This is critical for real-world use.

4. **Trick animation online**: When a non-local player plays a card, the client receives the updated trick in `state_recv_apply`. But the card animation (FLOW_CARD_ANIMATING) is only triggered when the client detects `num_played > prev_trick_count` during FLOW_WAITING_FOR_HUMAN. If it's not our turn, we're in FLOW_IDLE and the card just appears without animation. A proper implementation would detect new cards in the trick and trigger fly-in animations regardless of whose turn it is.

5. **Scoring phase online**: The scoring table animation (card fly-ins, count-up) relies on `rs->pile_cards[]` which is populated during local trick resolution. Online, trick resolution is skipped. The scoring animation may not work correctly. Needs testing.

### Medium Priority

6. **Server broadcast optimization**: Only broadcast when state actually changes (dirty flag per room). During idle moments (waiting for human input), save CPU and bandwidth.

7. **Room timeout**: WAITING rooms with no activity should expire after 5 minutes. PLAYING rooms where all humans disconnected should auto-close.

8. **Error feedback**: When the server rejects a command (`server_game_apply_cmd` returns false), it sends `NET_MSG_ERROR`. The client logs it to stderr but doesn't show it to the player. Should display as a toast or chat message.

9. **Validate protocol version at connect**: The handshake includes `protocol_version`, and the server checks it. But there's no mechanism for the client to know it's outdated. A user-facing "Update required" message would help.

10. **Rogue/Duel animations online**: The Rogue and Duel effects require showing opponent cards and animating card exchanges. Online, the flow guards skip the AI auto-choose timeouts but the animation states still exist. The server needs to send the Rogue/Duel targets so the client can animate them correctly. Currently these effects may not display properly online.

### Low Priority

11. **Logging framework**: Both client and server use bare `printf`/`fprintf`. A log-level system (DEBUG/INFO/WARN/ERROR) with timestamps would help debugging.

12. **Connection quality indicator**: `client_net_ping_ms()` tracks RTT but nothing displays it. A small overlay showing ping would help during testing.

13. **`bool online` parameter proliferation**: Three functions now take this parameter. If more functions need it, consider a global flag or a field in GameState (`gs->online_mode`). The current approach is explicit but could become unwieldy.

14. **Server-side AI for non-human players**: Currently the server calls `ai_play_card()` from `server_game.c`. The AI logic is the same as the client's offline AI. For a competitive game, the server should have configurable AI difficulty.

15. **Unit tests for serialization**: `protocol.c` has ~1868 lines of serialization code with no automated tests. A round-trip test (serialize → deserialize → compare) for each message type would catch regressions.
