# Hollow Hearts

A Hearts modification, built in C with Raylib. Players engage with Contracts, Vendettas, and Transmutation Cards that add strategic depth to the classic trick-taking game.

**THIS GAME IS FOR 4 HUMAN PLAYERS OVER THE NETWORK. The server is authoritative — never trust the client.**

## Game Vision & Roadmap

### Phase 1: Vanilla Hearts — COMPLETE

Standard Hearts game with full trick-taking, scoring, pass phase, and game-over detection. All engine systems designed with extension points for Phase 2.

### Phase 2: Hollow Hearts Modifications — COMPLETE

- **Contracts**: Per-round secret missions drafted from pairs. Completing grants Transmutation Cards.
- **Transmutation Cards**: Persistent inventory, applied during pass phase. Special properties (ALWAYS_WIN, ALWAYS_LOSE, multi-suit, fog, custom points). Definitions in `assets/defs/transmutations.json`.
- **Dealer**: Highest scorer becomes Dealer, chooses pass direction and amount.
- **Phase 2 effects**: Rogue (reveal opponent card), Duel (exchange cards), Shield, Curse, Anchor, Binding, Parasite, Bounty, Inversion, Mirror.

### Phase 3: Networking — CURRENT

Online multiplayer with dedicated servers, lobby/matchmaking, accounts, and anti-cheat. See **`NETWORKING.md`** for the full 22-step macro-plan.

**Architecture**: Dedicated Game Server (`hh-server`) + Lobby Server (`hh-lobby`) + Client (`hollow-hearts`). Server code never ships to players. Server never sends opponents' hidden card identities to clients.

### Engine Design Principle

**Server-authoritative, client-rendering.** The game server owns all game state and validates every action. Clients send `InputCmd` messages and receive state updates. The existing Command Pattern (`InputCmd` tagged union with `source_player`) maps directly to network messages. The `core/` layer (no Raylib dependencies) runs headless on the server.

## Build

```sh
make          # client release build (hollow-hearts)
make debug    # client debug build (-DDEBUG -g -O0, enables debug cheats)
make server   # game server (hh-server) — no Raylib
make lobby    # lobby server (hh-lobby) — no Raylib
```

## Tech

- Language: C (C11)
- Graphics: Raylib (client only)
- Networking: POSIX TCP sockets
- Database: SQLite (lobby server only)
- Build: Make

## Conventions

- Format with `clang-format` (project `.clang-format` when added)
- Prefer simple, readable C over clever tricks
- Naming: `snake_case` for functions/variables, `PascalCase` for types, `UPPER_CASE` for constants/macros

## Development Workflow

Every feature or significant change follows a 5-step pipeline using subagents. The main agent orchestrates the workflow and writes all implementation code.

### Step 1: Plan — `@game-developer`

Before implementing any new system, feature, or multi-file change, consult `@game-developer`. This agent:
- Analyzes the request against the current codebase
- Reads architecture docs from `.claude/docs/`
- Produces a structured plan with types, function signatures, integration points, and a checklist

**When to use:** Complex tasks or when entering Plan Mode. New features, new game systems, complex changes touching 2+ files, architectural decisions.
**When to skip:** Trivial bug fixes, single-line changes, formatting, comments. **Also skip when executing an already-established plan** — these steps are for creating the plan, not for re-planning during implementation.

### Step 2: Route — `@architecture-router`

Immediately after `@game-developer` produces a plan and before the main agent begins writing, run `@architecture-router`. This agent:
- Reads `ARCHITECTURE.md` and the file tree to determine where every planned item (function, struct, enum) belongs
- Produces a Routing Table mapping each item to a target file with a rationale
- Proposes new files if no existing file fits, and flags mixed-concern or bloat risks
- Does NOT write or modify code — it only decides where code belongs

The main agent **must** receive the Routing Table as a constraint before implementing.

**When to use:** Whenever `@game-developer` produces a plan (i.e., new features, new systems, multi-file changes).
**When to skip:** Same as `@game-developer`. **Also skip when executing an already-established plan.**

### Step 3: Implement — Main Agent

The main agent executes the plan from `@game-developer`, constrained by the Routing Table from `@architecture-router`:
- Follow the checklist item by item
- Create/modify files as specified in the plan
- Leverage auto-triggered C skills (`c-data-structures`, `c-memory-management`, `c-systems-programming`) for correct patterns — write it right the first time to minimize audit rework
- Use **Context7** MCP server to verify Raylib API signatures before calling them (client code only)
- Build with `make` / `make server` / `make lobby` to verify compilation

### Step 4: Map Dependencies — `@dependency-mapper`

After implementation, run `@dependency-mapper` to update the dependency graph. This agent:
- Updates `.claude/deps.json` with new/changed dependencies
- Adds/updates in-file dependency headers in `.h` and `.c` files
- Emits an impact alert if the change could break other files

**When to use:** Any change to headers, structs, enums, typedefs, function signatures, or file creation/deletion.
**When to skip:** Implementation-only changes inside function bodies, comments, formatting.

### Step 5: Review — `@code-auditor` + `@architecture-router`

After implementation (and dependency mapping if applicable), run `@code-auditor` to review. This agent checks:
- Correctness, memory safety, performance
- C best practices, naming conventions
- Raylib-specific patterns

Fix any critical/warning issues before considering the task complete.

Also run `@architecture-router` in review mode: it reads the modified files and produces an **architecture report** for the user, verifying that the plan was executed correctly and file boundaries were respected. It does NOT write or edit any code — it only provides a read-only report to the human.

### Workflow Summary

```
Feature Request
    │
    ▼                        ┐
@game-developer       →  Structured Plan            │ Planning only
    │                                                │ (complex tasks / Plan Mode)
    ▼                                                │ Skip when executing an
@architecture-router  →  Routing Table               │ already-established plan
    │                        ┘
    ▼
Main Agent            →  Implementation + `make`
    │
    ▼
@dependency-mapper    →  deps.json + impact report (if headers/types changed)
    │
    ▼
@code-auditor         →  Audit report → fix issues
    │
    ▼
@architecture-router  →  Architecture report (read-only, for the human)
```

## Project Structure

```
hollow-hearts/
├── CLAUDE.md              # This file
├── NETWORKING.md          # Full networking macro-plan (22 steps)
├── Makefile               # Build system (client, server, lobby targets)
├── src/
│   ├── main.c             # Client entry point, top-level loop wiring
│   ├── core/              # Pure game logic — no Raylib, no rendering, no networking
│   │   ├── card.c/h       # Card type, suit/rank helpers, point values
│   │   ├── hand.c/h       # Hand container (add, remove, sort, move, query)
│   │   ├── deck.c/h       # Deck (shuffle, deal)
│   │   ├── trick.c/h      # Trick (play card, determine winner)
│   │   ├── player.c/h     # Player struct
│   │   ├── game_state.c/h # GameState, phase enum, round/trick state
│   │   ├── input.c/h      # Input abstraction (poll, command queue — InputCmd tagged union)
│   │   ├── clock.c/h      # Game clock / time scaling
│   │   └── settings.c/h   # Persistent game settings
│   ├── net/               # Shared networking code (client + server + lobby)
│   │   ├── protocol.c/h   # Message types, binary serialization, framing
│   │   ├── socket.c/h     # Non-blocking TCP wrappers, poll-based multiplexing
│   │   ├── client_net.c/h # Client connection manager (connect, handshake, send/recv)
│   │   ├── cmd_send.c/h   # Serialize and send InputCmds to server
│   │   ├── state_recv.c/h # Receive and apply server state updates
│   │   └── reconnect.c/h  # Reconnection with exponential backoff
│   ├── server/            # Game server (hh-server) — headless, no Raylib
│   │   ├── server_main.c  # Server entry point, headless game loop
│   │   ├── server_game.c/h # Server-side game tick, command validation
│   │   ├── server_net.c/h # Accept connections, route messages, broadcast state
│   │   ├── room.c/h       # Room management (create, join, leave, lifecycle)
│   │   └── lobby_link.c/h # Communication with lobby server
│   ├── lobby/             # Lobby server (hh-lobby) — headless, no Raylib
│   │   ├── lobby_main.c   # Lobby entry point
│   │   ├── lobby_net.c/h  # Accept connections, route lobby messages
│   │   ├── db.c/h         # SQLite setup, migrations, queries
│   │   ├── auth.c/h       # Account registration, login, token management
│   │   ├── rooms.c/h      # Room code generation, tracking, expiration
│   │   ├── matchmaking.c/h # Matchmaking queue (FIFO, future ELO-based)
│   │   └── server_registry.c/h # Track active game servers, load balancing
│   ├── render/            # Everything visual — owns Raylib calls (client only)
│   │   ├── render.c/h     # RenderState, sync, update, draw, hit-testing, drag API
│   │   ├── anim.c/h       # Animation engine (start, update, toss, bezier, timing constants)
│   │   ├── easing.c/h     # Easing math (ease_apply, lerpf)
│   │   ├── layout.c/h     # Layout math (positions, rects, scaling — no drawing)
│   │   ├── card_dimens.h  # Shared card dimension constants (CARD_WIDTH_REF, etc.)
│   │   ├── card_render.c/h # Card sprite drawing (face, back, procedural fallback)
│   │   └── particle.c/h   # Particle system (init, spawn, update, draw)
│   ├── game/              # Game flow — bridges core logic and render (client only)
│   │   ├── ai.c/h         # AI decision logic (used by server for disconnected players)
│   │   ├── process_input.c/h # Translates mouse/key events into InputCmds
│   │   ├── update.c/h     # Per-frame game state updates
│   │   ├── turn_flow.c/h  # Turn/trick FSM (FlowStep)
│   │   ├── play_phase.c/h # Playing phase rules
│   │   ├── pass_phase.c/h # Passing phase rules
│   │   ├── phase_transitions.c/h # Phase change orchestration
│   │   ├── info_sync.c/h  # Syncs info panel / playability to RenderState
│   │   ├── settings_ui.c/h # Settings screen logic
│   │   ├── login_ui.c/h   # Login/register screen
│   │   └── online_ui.c/h  # Online menu, room browser, matchmaking UI
│   ├── audio/
│   │   └── audio.c/h      # Sound effects and music
│   ├── phase2/            # Hollow Hearts modification systems
│   │   ├── contract.h / contract_logic.c/h
│   │   ├── transmutation.h / transmutation_logic.c/h
│   │   ├── effect.h, phase2_state.h
│   │   ├── phase2_defs.c/h # Definition loading
│   │   └── json_parse.c/h  # JSON parser wrapper
│   └── vendor/
│       └── cJSON.c/h      # Third-party JSON library
├── .claude/
│   ├── agents/            # Subagent definitions
│   │   ├── game-developer.md
│   │   ├── architecture-router.md
│   │   ├── dependency-mapper.md
│   │   └── code-auditor.md
│   ├── docs/              # Architecture reference documentation
│   │   ├── 00_INDEX.md
│   │   └── 01-14_*.md
│   └── deps.json          # Dependency map (managed by dependency-mapper)
```

## File Boundaries (strict)

Each file has a single responsibility. Do not leak logic across boundaries.

### `render/` rules

| File | Owns | Does NOT contain |
|------|------|------------------|
| **render.c** | RenderState, sync_hands, render_update/draw, hit-testing, drag state management (start/commit/cancel/snap) | Animation math, easing curves, layout position calculations, card sprite drawing |
| **anim.c** | Animation engine: anim_start, anim_update, anim_setup_toss, speed control, all animation timing constants | Drawing, layout, hit-testing, game state |
| **easing.c** | Pure math: easing curves (ease_apply), lerpf | Anything Raylib, anything stateful |
| **layout.c** | Pure position/rect math: where things go on screen, scaling | Drawing, animation, game state, RenderState |
| **card_render.c** | Drawing a single card (face/back, sprite or procedural) | Layout, animation, game logic |
| **particle.c** | Particle lifecycle (spawn, update, draw) | Cards, layout, game state |

**Key principle:** `render.c` orchestrates but delegates. It calls `layout_*()` for positions, `anim_*()` for animation, `card_render_*()` for drawing. If you need a new position calculation, add it to `layout.c`. If you need a new animation or timing constant, add it to `anim.c/h`. If you need new easing math, add it to `easing.c`.

### `game/` rules

| File | Owns | Does NOT contain |
|------|------|------------------|
| **process_input.c** | Translating raw input into InputCmds and calling render drag API | Animation, layout math, direct CardVisual manipulation |
| **update.c** | Per-frame game state mutation, phase-specific logic dispatch | Rendering, drawing, input polling |
| **turn_flow.c** | Turn/trick state machine (whose turn, AI dispatch, trick resolution) | Rendering, input |
| **info_sync.c** | Copying game state into RenderState display fields (info panel, playability flags) | Game logic decisions, drawing |

**Key principle:** `game/` files read/write GameState and call into `render/` public API. They never manipulate CardVisual fields directly or compute layout positions (except `process_input.c` using `layout_trick_position` for toss classification, which is a geometry query).

### `core/` rules

`core/` is pure game logic. No Raylib includes, no render types, no visual concepts, no network I/O. If a function needs screen positions, card visuals, or socket operations, it does not belong in `core/`. Both the client and the server link `core/`.

### `net/` rules

| File | Owns | Does NOT contain |
|------|------|------------------|
| **protocol.c** | Message type enum, binary serialization/deserialization, length-prefix framing | Socket I/O, game logic, rendering |
| **socket.c** | Non-blocking TCP wrappers, poll-based multiplexing, connection state machine, ring buffers | Message semantics, game state, protocol interpretation |
| **client_net.c** | Client-side connection lifecycle (connect, handshake, reconnect), send/recv integration with main loop | Server logic, rendering, game rules |
| **cmd_send.c** | Serialize `InputCmd` and send to server, filter rendering-only commands | Command processing, game state mutation |
| **state_recv.c** | Receive `MSG_STATE_UPDATE`, apply to local `GameState`, trigger `sync_needed` | Rendering, command generation |

**Key principle:** `net/` handles the wire protocol and connection management. It serializes/deserializes `InputCmd` and game state but never makes game logic decisions. `protocol.c` defines the format, `socket.c` handles transport, other files handle client-specific flows.

### `server/` rules

| File | Owns | Does NOT contain |
|------|------|------------------|
| **server_main.c** | Server entry point, headless fixed-timestep loop, signal handling | Raylib, rendering, client UI |
| **server_game.c** | Server-side game tick, InputCmd validation, state broadcast decisions | Socket I/O (uses `net/`), rendering |
| **server_net.c** | Accept connections, authenticate tokens, route messages to rooms, broadcast | Game logic, room lifecycle |
| **room.c** | Room struct, create/join/leave, player slot management, disconnect timers, AI fallback | Networking, protocol details |
| **lobby_link.c** | Communication with lobby server (register, report results, heartbeat) | Game logic, room management |

**Key principle:** `server/` orchestrates game rooms and validates all player actions. It links `core/` for game logic and `net/` for transport. It never includes Raylib headers. The server is the single source of truth — it never trusts client-provided game state.

### `lobby/` rules

| File | Owns | Does NOT contain |
|------|------|------------------|
| **lobby_main.c** | Lobby entry point, event loop | Game logic, rendering |
| **db.c** | SQLite connection, schema migrations, parameterized queries | Business logic, networking |
| **auth.c** | Registration, login, password hashing, token generation/validation | Database schema, networking |
| **rooms.c** | Room code generation, code→server mapping, expiration | Game logic, matchmaking |
| **matchmaking.c** | Queue management, player grouping, room creation triggers | Authentication, database |
| **server_registry.c** | Track active game servers, health checks, load-based selection | Game logic, authentication |

**Key principle:** `lobby/` manages player identity and game discovery. It never touches game state or `core/`. It communicates with game servers via `net/` protocol to create rooms and receive results.

### Anti-cheat boundary (critical)

The server MUST NOT send opponents' hidden card identities to any client. State updates include:
- **Own hand**: full card identities
- **Opponents' hands**: card count only (no suit/rank)
- **Current trick**: face-up cards only (all players can see played cards)
- **Scores, phase, turn info**: public knowledge

This is enforced in `server_game.c` when building state update messages.

## Reference Resources

- **Networking plan** — `NETWORKING.md` at project root. The full 22-step macro-plan for Phase 3. Reference for current step, architecture decisions, and file assignments.
- **Architecture docs** — `.claude/docs/00_INDEX.md` for the full index. Covers subsystem lifecycle, memory management, game loops, events, input — all adapted for C with Raylib.
- **Context7 MCP** — Global MCP server for Raylib API reference. Use to verify function signatures, parameter order, and return types. Available to main agent and all subagents. (Client code only — server/lobby don't use Raylib.)
- **C Skills** — Three auto-triggered skills (`c-data-structures`, `c-memory-management`, `c-systems-programming`) provide C patterns and best practices. Used by the main agent during implementation, not needed by code-auditor (covered by its prompt).
- **Clangd LSP** — Enabled via `clangd-lsp` plugin. Provides go-to-definition, find-references, hover, document/workspace symbols, and call hierarchy. Requires `compile_commands.json` in repo root (generated by build). Use for navigating types, tracing call chains, and verifying symbol usage before refactoring.
