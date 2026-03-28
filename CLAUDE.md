# Hollow Hearts

A Hearts card game with Contracts, Vendettas, and Transmutation Cards, built in C with Raylib. 4 human players over the network. Server-authoritative architecture — never trust the client.

**Three binaries**: Client (`hollow-hearts`), Game Server (`hh-server`), Lobby Server (`hh-lobby`).

**Current phase: Debugging.** All features are implemented. No new systems or files should be created unless a bug fix absolutely requires it.
Main source of bugs: Networking implementation. Networking bugs.

## Build

```sh
make          # client release build
make debug    # client debug build (-DDEBUG -g -O0)
make server   # game server — no Raylib
make lobby    # lobby server — no Raylib
```

## Conventions

- C11, `snake_case` functions/variables, `PascalCase` types, `UPPER_CASE` constants/macros
- Prefer simple, readable C over clever tricks

## Debugging Workflow

1. **Diagnose** — Reproduce the bug, read relevant source files, trace the issue
2. **Fix** — Main agent implements the fix, builds with `make` / `make server` / `make lobby`
3. **Audit** — Run `@code-auditor` to review the fix for correctness, memory safety, and C best practices
4. **Map** — If the fix changed headers, structs, enums, typedefs, or function signatures, run `@dependency-mapper` to update `.claude/deps.json`

## Navigating the Codebase

Use the **file tree** below and **`.claude/deps.json`** to locate code quickly. `deps.json` maps every file to its exported symbols, requirements, and reverse dependencies — grep it to find where a type or function lives without reading source files.

```
hollow-hearts/
├── src/
│   ├── main.c             # Client entry point, top-level loop wiring
│   ├── core/              # Pure game logic — no Raylib, no rendering, no networking
│   │   ├── card.c/h       # Card type, suit/rank helpers, point values
│   │   ├── hand.c/h       # Hand container (add, remove, sort, move, query)
│   │   ├── deck.c/h       # Deck (shuffle, deal)
│   │   ├── trick.c/h      # Trick (play card, determine winner)
│   │   ├── player.c/h     # Player struct
│   │   ├── game_state.c/h # GameState, phase enum, round/trick state
│   │   ├── input.c/h      # Input abstraction (poll, command queue)
│   │   ├── input_cmd.h    # InputCmd tagged union — the command protocol
│   │   ├── clock.c/h      # Game clock / time scaling
│   │   └── settings.c/h   # Persistent game settings
│   ├── net/               # Shared networking code (client + server + lobby)
│   │   ├── protocol.c/h   # Message types, binary serialization, framing
│   │   ├── socket.c/h     # Non-blocking TCP wrappers, poll-based multiplexing
│   │   ├── client_net.c/h # Client connection manager (connect, handshake, send/recv)
│   │   ├── cmd_send.c/h   # Serialize and send InputCmds to server
│   │   ├── state_recv.c/h # Receive and apply server state updates
│   │   ├── lobby_client.c/h # Client-side lobby communication
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
│   │   ├── matchmaking.c/h # Matchmaking queue
│   │   └── server_registry.c/h # Track active game servers, load balancing
│   ├── render/            # Everything visual — Raylib calls (client only)
│   │   ├── render.c/h     # RenderState, sync, update, draw, hit-testing, drag API
│   │   ├── anim.c/h       # Animation engine (start, update, toss, timing constants)
│   │   ├── easing.c/h     # Easing math (ease_apply, lerpf)
│   │   ├── layout.c/h     # Layout math (positions, rects, scaling — no drawing)
│   │   ├── card_dimens.h  # Shared card dimension constants
│   │   ├── card_render.c/h # Card sprite drawing (face, back, procedural fallback)
│   │   └── particle.c/h   # Particle system
│   ├── game/              # Game flow — bridges core logic and render (client only)
│   │   ├── ai.c/h         # AI decision logic (also used server-side for disconnected players)
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
│   ├── phase2/            # Hollow Hearts modifications (contracts, transmutations, effects)
│   │   ├── contract.h / contract_logic.c/h
│   │   ├── transmutation.h / transmutation_logic.c/h
│   │   ├── effect.h, phase2_state.h
│   │   ├── phase2_defs.c/h # Definition loading
│   │   └── json_parse.c/h  # JSON parser wrapper
│   └── vendor/
│       └── cJSON.c/h      # Third-party JSON library
├── .claude/
│   ├── agents/            # code-auditor.md, dependency-mapper.md
│   └── deps.json          # Dependency map — every public symbol, requires, used_by
```

## File Boundaries (condensed)

- **`core/`** — Pure game logic. No Raylib, no render types, no network I/O. Linked by both client and server.
- **`render/`** — render.c orchestrates; delegates to layout.c (positions), anim.c (animation), card_render.c (drawing), easing.c (math). No game logic.
- **`game/`** — Bridges core and render. Reads/writes GameState, calls render public API. Never manipulates CardVisual directly.
- **`net/`** — Wire protocol and connection management. Serializes InputCmd and game state. No game logic decisions.
- **`server/`** — Authoritative game rooms. Links core/ for logic, net/ for transport. No Raylib. Never trusts clients.
- **`lobby/`** — Player identity and matchmaking. No game state, no core/.

## Anti-cheat Boundary (critical)

The server MUST NOT send opponents' hidden card identities to any client. State updates include:
- **Own hand**: full card identities
- **Opponents' hands**: card count only (no suit/rank)
- **Current trick**: face-up cards only (played cards are public)
- **Scores, phase, turn info**: public knowledge

Enforced in `server_game.c` when building state update messages.

## Reference Resources

- **Context7 MCP** — Raylib API reference. Verify function signatures before calling them (client code only).
- **Clangd LSP** — go-to-definition, find-references, hover, symbols, call hierarchy. Requires `compile_commands.json`.
