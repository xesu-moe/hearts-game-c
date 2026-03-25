# Networking Macro-Plan for Hollow Hearts

## Context

Hollow Hearts is designed for 4 human players but currently runs as a single-process local game. Networking will enable online multiplayer with anti-cheat guarantees, matchmaking, and persistent player stats.

## Architecture Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Architecture | Dedicated Server + Lobby Server + Client | Anti-cheat (server never ships to players), fair for all players |
| Binaries | `hollow-hearts` (client), `hh-server` (game), `hh-lobby` (lobby) | Separate scaling, server code never in client hands |
| Language | All C, same repo | Consistent tooling, shared code via `src/net/` |
| Transport | TCP sockets | Reliable ordered delivery, perfect for turn-based card game |
| Lobby | Room codes (private) + matchmaking (random) | Covers both friend groups and solo queue |
| Database | SQLite | Zero-config, embedded, sufficient for our scale |
| Persistence | Accounts + stats + match history | ELO-based matchmaking, player identity |
| AI policy | Wait for 4 humans → on disconnect, 30s grace → AI fills slot → player can rejoin and replace AI |
| Granularity | Fine-grained (~20 steps) | 1-2 files per step, easy to verify |

## Component Overview

```
┌─────────────┐       ┌─────────────┐       ┌─────────────┐
│  hh-lobby   │◄─TCP─►│  hh-server  │◄─TCP─►│   client    │
│  (lobby +   │       │  (game      │       │  (Raylib +  │
│  accounts + │       │   rooms +   │       │   render +  │
│  matchmake) │       │   authority)│       │   input)    │
│  [SQLite]   │       │             │       │             │
└─────────────┘       └─────────────┘       └─────────────┘
```

**Flow**: Client → Lobby (login, find game) → Lobby assigns to Game Server → Client connects to Game Server → Play

---

## Macro Steps

### Phase A: Network Foundation (shared code)

**Step 1 — Message Protocol Definition**
- Define binary message format with length-prefix framing
- Define message types enum: `MSG_HANDSHAKE`, `MSG_INPUT_CMD`, `MSG_STATE_UPDATE`, `MSG_PHASE_CHANGE`, `MSG_CHAT`, `MSG_ROOM_JOIN`, `MSG_ROOM_LEAVE`, `MSG_ERROR`, etc.
- Serialize/deserialize `InputCmd` (already a tagged union — map to wire format)
- Serialize/deserialize game state snapshots (hands, trick, scores, phase, turn)
- Files: `src/net/protocol.h`, `src/net/protocol.c`
- **Verify**: Unit test — round-trip serialize/deserialize of every InputCmd variant

**Step 2 — TCP Socket Abstraction**
- Non-blocking TCP wrappers: listen, accept, connect, send, recv
- Length-prefix framing (4-byte header + payload) to handle TCP stream reassembly
- Connection state machine: DISCONNECTED → CONNECTING → CONNECTED → CLOSING
- Per-connection send/recv ring buffers
- `select()`/`poll()` based multiplexing (no threads needed for our scale)
- Files: `src/net/socket.h`, `src/net/socket.c`
- **Verify**: Two test programs that connect and exchange messages

**Step 3 — Makefile & Project Structure**
- Add `hh-server` target (links `src/core/`, `src/net/`, `src/server/`, `src/phase2/`, `src/vendor/` — NO Raylib)
- Add `hh-lobby` target (links `src/net/`, `src/lobby/`, `src/vendor/` — NO Raylib)
- `hollow-hearts` (client) target adds `src/net/` to existing sources
- Conditional compilation: `#ifdef HH_SERVER` / `#ifdef HH_LOBBY` / `#ifdef HH_CLIENT`
- Directory structure: `src/net/`, `src/server/`, `src/lobby/`
- **Verify**: `make`, `make server`, `make lobby` all compile (even if binaries do nothing yet)

---

### Phase B: Game Server (`hh-server`)

**Step 4 — Headless Game Loop**
- `src/server/server_main.c` — entry point, no Raylib, no rendering
- Runs `GameState` + `Phase2State` + `TurnFlow` + `PassPhaseState` with a fixed-timestep loop
- Replaces `input_poll()` / `process_input()` with network command ingestion
- Server ticks at 60Hz (same `FIXED_DT`), processes buffered remote commands
- Files: `src/server/server_main.c`, `src/server/server_game.h/c`
- **Verify**: Server starts, creates a GameState, ticks without crashing

**Step 5 — Server Room Management**
- Room struct: room code, 4 player slots, `GameState`, phase, status (WAITING/PLAYING/FINISHED)
- Create room (generate 4-char code), join room (by code), leave room
- Player slot struct: connection handle, player_id (0-3), auth token, status (CONNECTED/DISCONNECTED/AI)
- Room lifecycle: WAITING (lobby) → PLAYING (game) → FINISHED (report results) → cleanup
- Max rooms per server instance (configurable, e.g., 100)
- Files: `src/server/room.h`, `src/server/room.c`
- **Verify**: Create room, join 4 fake connections, room transitions to PLAYING

**Step 6 — Server Network Loop**
- Accept client connections on configured port
- Authenticate via token (from lobby server)
- Route messages to the correct room based on connection→room mapping
- Receive `MSG_INPUT_CMD` → deserialize → validate → apply to room's GameState
- After state mutation → serialize state update → broadcast to all 4 clients in room
- Handle connection drops gracefully (mark slot as DISCONNECTED, start 30s timer)
- Files: `src/server/server_net.h/c`
- **Verify**: 4 netcat/test clients connect, join room, server logs activity

---

### Phase C: Client Networking

**Step 7 — Client Connection Manager**
- New module that manages connection to game server
- Connect to IP:port, perform handshake (send auth token, receive player_id assignment)
- Background send/recv integrated into main loop (non-blocking, called each frame)
- Connection state exposed to UI (connecting, connected, disconnected, reconnecting)
- Files: `src/net/client_net.h`, `src/net/client_net.c`
- **Verify**: Client connects to server, handshake completes, connection stable

**Step 8 — Command Forwarding (Client → Server)**
- Intercept `InputCmd` after `process_input()` — instead of local `game_update()`, serialize and send to server
- Only send commands relevant to the local player (player_id assigned at handshake)
- Client does NOT process own commands locally (server is authoritative)
- Filter out rendering-only commands (hover, drag) — those stay local
- Files: modify `src/game/process_input.c`, add `src/net/cmd_send.h/c`
- **Verify**: Client plays a card → server receives and logs the InputCmd

**Step 9 — State Application (Server → Client)**
- Client receives `MSG_STATE_UPDATE` containing: phase, hands (only own cards face-up, others card-count only), current trick, scores, turn info, pass state
- Apply to local `GameState` — overwrite with server-authoritative state
- Trigger `rs->sync_needed = true` to rebuild visuals
- Handle hidden information: server only sends card identities for cards the client should see
- Files: `src/net/state_recv.h/c`, modify `src/game/update.c`
- **Verify**: Server deals cards → client receives hand → cards render correctly

**Step 10 — Phase & Turn Synchronization**
- Server drives all phase transitions — clients follow
- Pass phase: server collects all 4 players' pass selections, then resolves and broadcasts
- Play phase: server enforces turn order, validates card legality, resolves tricks
- Scoring: server computes scores, sends results
- Client-side timers are display-only (server enforces actual timeouts)
- Files: modify `src/game/phase_transitions.c`, `src/game/turn_flow.c`
- **Verify**: Full round plays through over network — deal → pass → 13 tricks → scoring

---

### Phase D: Robustness

**Step 11 — Disconnect & Reconnect**
- Server: on connection drop, start 30s timer. Mark player as DISCONNECTED.
- Server: after 30s, switch slot to AI mode. AI makes decisions using existing `ai_play_card()` / `ai_select_pass()`.
- Server: if original player reconnects within grace period or after AI takeover, restore their slot. Sync full game state to reconnected client.
- Client: detect disconnect, show reconnecting UI, attempt reconnect with exponential backoff.
- Files: modify `src/server/room.c`, `src/server/server_net.c`, add `src/net/reconnect.h/c`
- **Verify**: Kill client mid-game → AI takes over → restart client → rejoins game in progress

**Step 12 — Network Chat**
- RenderState already has `chat_msgs[]` ring buffer
- `MSG_CHAT` message type: player_id + text
- Server relays chat to all players in room (with basic sanitization)
- Client sends chat via new input field, receives and displays in existing chat UI
- Files: modify `src/net/protocol.c`, add chat handling to server and client
- **Verify**: Player sends chat message → appears on all 4 clients

**Step 13 — Error Handling & Edge Cases**
- Server validates every InputCmd (wrong phase, wrong turn, illegal card, duplicate play)
- Server sends `MSG_ERROR` with human-readable reason on rejection
- Client shows error in chat/toast
- Handle: player sends command for wrong phase, server full, room full, invalid room code
- Timeout: if all humans disconnect, room auto-closes after 5 min
- Files: modify `src/server/server_game.c`, `src/net/protocol.h`
- **Verify**: Send invalid commands → server rejects cleanly, game continues

---

### Phase E: Lobby Server (`hh-lobby`)

**Step 14 — Lobby Server Foundation**
- `src/lobby/lobby_main.c` — entry point, TCP listener (could add HTTP later)
- SQLite setup: create database, tables (accounts, match_history, stats)
- Accept client connections, route by message type
- Lobby protocol: `MSG_REGISTER`, `MSG_LOGIN`, `MSG_LOGOUT`, `MSG_CREATE_ROOM`, `MSG_JOIN_ROOM`, `MSG_QUEUE_MATCHMAKE`, `MSG_LIST_SERVERS`
- Files: `src/lobby/lobby_main.c`, `src/lobby/lobby_net.h/c`, `src/lobby/db.h/c`
- **Verify**: Lobby starts, creates SQLite DB, accepts connections

**Step 15 — Account System (Keypair Auth)**
- Crypto: Ed25519 via TweetNaCl (`tweetnacl.c/h`, single-file public domain library)
- Client generates Ed25519 keypair on first launch, stores private key locally (`~/.hollow-hearts/identity.key`)
- Register: client sends `{username, public_key}` → lobby stores; rejects if username taken or public key already registered
- Login (challenge-response): client sends `{username}` → lobby sends random 32-byte nonce → client signs nonce with private key → lobby verifies signature against stored public key → issues session token (random 32-byte hex)
- Token validation: game server asks lobby to validate token on client connect (unchanged)
- Account table: `id`, `username`, `public_key` (32 bytes), `created_at`, `last_login`
- Client-side key management: `src/net/identity.h/c` — generate keypair, load/save from disk, sign challenges
- Files: `src/lobby/auth.h/c`, `src/net/identity.h/c`, `src/vendor/tweetnacl.c/h`
- **Verify**: First launch generates keypair → register with username → login via challenge-response → receive token → use token to join game server

**Step 16 — Room Code System**
- Lobby generates 4-6 char alphanumeric room codes (exclude ambiguous chars like O/0/I/l)
- Lobby tracks: code → game_server_address + room_id, creation time, status
- Codes expire after game ends or 10 min unused
- Client requests room creation → lobby picks a game server → tells game server to create room → returns code to client
- Client joins by code → lobby resolves to game server address → client connects directly
- Files: `src/lobby/rooms.h/c`
- **Verify**: Create room → get code → another client joins by code → both in same game server room

**Step 17 — Matchmaking Queue**
- Simple FIFO queue: players enter queue, when 4 are waiting, create a room
- Future: ELO-based matching (group players within rating range)
- Queue timeout: if no match in 60s, widen search or notify client
- Lobby selects game server with lowest load
- Files: `src/lobby/matchmaking.h/c`
- **Verify**: 4 clients queue → lobby creates room → all 4 connected and playing

**Step 18 — Lobby ↔ Game Server Communication**
- Game server registers with lobby on startup (sends its address + capacity)
- Lobby sends: create room, room status query
- Game server reports: room started, room finished (with results for stats), player disconnected/reconnected
- Heartbeat: game server pings lobby periodically, lobby removes dead servers
- Files: `src/server/lobby_link.h/c`, `src/lobby/server_registry.h/c`
- **Verify**: Game server registers → lobby assigns rooms → game finishes → lobby records result

---

### Phase F: Client Lobby Integration

**Step 19 — Login & Register UI**
- New game phase: `PHASE_LOGIN` (before `PHASE_MENU`)
- Username text input field (no password — keypair auth is automatic)
- Register / Login buttons (both use the locally stored keypair)
- First-launch flow: auto-generate keypair, prompt for username, register
- Error display (username taken, invalid signature, key not found, etc.)
- On success: store token, transition to PHASE_MENU
- Files: `src/game/login_ui.h/c`, modify `src/core/game_state.h` (add phase)
- **Verify**: First launch → enter username → register → auto-login → reaches menu; subsequent launches → auto-login → reaches menu

**Step 20 — Online Menu & Room UI**
- Menu gets new options: "Play Online" (opens online submenu)
- Online submenu: "Create Room" / "Join Room" / "Quick Match"
- Create Room: shows room code, waiting screen with player list
- Join Room: text input for code, join button
- Quick Match: "Searching..." with cancel button
- Waiting room: shows 4 slots filling up, starts game when full
- Files: `src/game/online_ui.h/c`, modify `src/game/update.c`
- **Verify**: Full flow — login → create room → share code → 3 others join → game starts

**Step 20.1 — Player Join Notifications & Waiting Room Polish**
- Game server sends `NET_MSG_SERVER_ROOM_STATUS` to lobby when a player joins/leaves a room (room code + current player count + player names)
- Lobby forwards player count/names to the room creator's client connection
- Client updates `OnlineUIState.player_count` and displays individual player slots in the Create Room waiting room
- Pong timestamp: populate `server_timestamp_ms` in lobby and game server pong responses (currently hardcoded to 0)
- Files: `src/server/server_net.c`, `src/net/protocol.h/c` (new message type), `src/lobby/lobby_net.c`, `src/game/online_ui.h`, `src/render/render.c`
- **Verify**: Create room → second client joins → creator sees "2/4 players" update live

---

### Phase G: Stats & Polish

**Step 21 — Stats & Match History**
- After game ends, server reports to lobby: winner, scores, rounds played, duration
- Lobby records in `match_history` table, updates player stats
- Stats table: `player_id`, `games_played`, `games_won`, `total_score`, `elo_rating`
- ELO calculation: standard formula, K-factor tuned for Hearts
- Client can view own stats from menu
- Files: `src/lobby/stats.h/c`, `src/game/stats_ui.h/c`
- **Verify**: Play a game → stats update in DB → viewable in client

**Step 22 — Final Polish**
- Connection quality indicator (ping display)
- Graceful server shutdown (notify clients)
- Rate limiting (prevent command spam)
- Input validation hardening (fuzzing)
- Test with real network conditions (latency, packet loss simulation)
- **Verify**: Full end-to-end test with 4 clients across network

---

## Key Files Summary

| Directory | Purpose |
|-----------|---------|
| `src/net/` | Shared networking: protocol, sockets, serialization |
| `src/server/` | Game server: headless loop, rooms, authority |
| `src/lobby/` | Lobby server: accounts, matchmaking, rooms, DB |
| `src/game/login_ui.c` | Client login screen |
| `src/game/online_ui.c` | Client online menu / waiting room |

## Risk Notes

- **Hidden information**: Server must NEVER send opponents' card identities to clients. Only send: card count, and face-up cards (current trick, own hand). This is the #1 anti-cheat requirement.
- **State divergence**: Client should always defer to server state. If client and server disagree, server wins.
- **SQLite concurrency**: SQLite handles concurrent reads well but writes serialize. Fine for our scale (<1000 concurrent users), but worth noting.
- **TCP head-of-line blocking**: For a card game with small messages and low frequency, this is a non-issue. If latency spikes become problematic, could switch to ENet later.
