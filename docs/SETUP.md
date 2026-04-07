# Hollow Hearts Online Setup Guide

Play Hollow Hearts over the network with 4 human players. Three processes are required: a **lobby server**, a **game server**, and **4 clients**.

## Architecture

```
  Client 1 ──┐                          ┌── Game Server
  Client 2 ──┼──── Lobby Server ────────┤   (hosts rooms,
  Client 3 ──┤     (accounts, matchmake) │    runs game logic)
  Client 4 ──┘                          └──
```

1. **Lobby Server** (`hh-lobby`) — manages accounts, matchmaking, and room assignment. Stores data in SQLite.
2. **Game Server** (`hh-server`) — hosts game rooms, validates all player actions, broadcasts state. Headless (no GUI).
3. **Client** (`hollow-hearts`) — renders the game, sends player input to the game server.

Flow: Clients log in to the lobby. The lobby assigns them to a game server. Clients connect directly to the game server for gameplay.

---

## Build

```bash
make          # Client (hollow-hearts) — requires Raylib
make server   # Game server (hh-server) — no Raylib needed
make lobby    # Lobby server (hh-lobby) — requires libsqlite3
```

All three binaries appear in the project root.

---

## Quick Start (Localhost)

Run all three processes on the same machine. Open 4 terminal windows for the clients.

### 1. Start the Lobby Server

```bash
./hh-lobby 7778
```

- Listens on port **7778**
- Creates `lobby.db` in the current directory (SQLite database for accounts/stats)

### 2. Start the Game Server

```bash
./hh-server 7777 127.0.0.1 7778
```

- Listens on port **7777** for client connections
- Connects to the lobby at `127.0.0.1:7778` and registers itself
- The lobby will direct clients to connect to `127.0.0.1:7777`

### 3. Start 4 Clients

```bash
./hollow-hearts --lobby-addr 127.0.0.1 --lobby-port 7778
```

Run this command 4 times (4 separate windows/terminals). Each client:
- Connects to the lobby server
- On first launch: prompts for a username (3+ characters, alphanumeric/underscore)
- Generates an Ed25519 keypair stored in `~/.hollow-hearts/identity.key`
- Saves your username in `~/.hollow-hearts/username.txt` for auto-login next time

### 4. Play

From any client's main menu, choose **Play Online**, then either:

- **Quick Match** — enters the matchmaking queue. Game starts automatically when 4 players are queued.
- **Create Room** — creates a private room and displays a 4-character room code. Share the code with friends.
- **Join Room** — enter a 4-character room code to join an existing room.

The game starts automatically when all 4 seats are filled.

---

## LAN Setup (Multiple Machines)

### Important: Server Address

The game server currently reports itself to the lobby as `127.0.0.1`. For LAN play, this means clients on other machines would be told to connect to `127.0.0.1` (their own localhost), which fails.

**Workaround:** Edit `src/server/server_main.c` line 106 and replace `"127.0.0.1"` with your game server's LAN IP:

```c
// Before:
lobby_link_init(lobby_addr, lobby_port, "127.0.0.1", port, MAX_ROOMS);

// After (example — use your actual IP):
lobby_link_init(lobby_addr, lobby_port, "192.168.1.50", port, MAX_ROOMS);
```

Then rebuild: `make server`

### Steps

Assuming the server machine's LAN IP is `192.168.1.50`:

**On the server machine:**
```bash
./hh-lobby 7778
./hh-server 7777 192.168.1.50 7778
```

**On each player machine:**
```bash
./hollow-hearts --lobby-addr 192.168.1.50 --lobby-port 7778
```

Ensure ports **7777** and **7778** are open in any firewalls.

---

## Command-Line Reference

### `hh-lobby [port] [db_path]`

| Argument | Default | Description |
|----------|---------|-------------|
| `port` | `9999` | TCP port to listen on |
| `db_path` | `lobby.db` | Path to SQLite database file (created if missing) |

### `hh-server [port] [lobby_addr lobby_port]`

| Argument | Default | Description |
|----------|---------|-------------|
| `port` | `7777` | TCP port to listen on for client connections |
| `lobby_addr` | *(none)* | Lobby server IP address |
| `lobby_port` | *(none)* | Lobby server port |

If `lobby_addr` and `lobby_port` are omitted, the game server runs standalone (no matchmaking, clients must know the IP/port directly).

### `hollow-hearts [--lobby-addr IP] [--lobby-port PORT]`

| Argument | Default | Description |
|----------|---------|-------------|
| `--lobby-addr` | `127.0.0.1` | Lobby server IP address |
| `--lobby-port` | `7778` | Lobby server port |

---

## Default Ports

| Service | Default Port | Note |
|---------|-------------|------|
| Lobby Server | `9999` | What `hh-lobby` listens on by default |
| Game Server | `7777` | What `hh-server` listens on by default |
| Client expects lobby at | `7778` | What `hollow-hearts` connects to by default |

**Note:** The lobby's default listen port (`9999`) does not match the client's default lobby port (`7778`). Either start the lobby on `7778` to match the client default, or pass `--lobby-port 9999` to the client.

---

## Authentication

Hollow Hearts uses **Ed25519 keypair authentication** (no passwords).

- On first launch, the client generates a keypair in `~/.hollow-hearts/identity.key`
- On registration, the public key is sent to the lobby and stored with your username
- On login, the lobby sends a random challenge nonce; the client signs it with the private key
- The lobby verifies the signature against the stored public key

**Your identity file is your account.** If you delete `~/.hollow-hearts/identity.key`, you lose access to that account. Back it up if you care about your stats/ELO.

### Per-Player Testing on One Machine

Each client instance shares `~/.hollow-hearts/`. To run 4 clients as different players on the same machine, override the home directory:

```bash
HOME=/tmp/player1 ./hollow-hearts --lobby-addr 127.0.0.1 --lobby-port 7778
HOME=/tmp/player2 ./hollow-hearts --lobby-addr 127.0.0.1 --lobby-port 7778
HOME=/tmp/player3 ./hollow-hearts --lobby-addr 127.0.0.1 --lobby-port 7778
HOME=/tmp/player4 ./hollow-hearts --lobby-addr 127.0.0.1 --lobby-port 7778
```

Each will generate its own identity and prompt for a unique username.

---

## Disconnect & Reconnect

- If a player disconnects mid-game, the server waits **30 seconds** before AI takes over their seat
- During the grace period, the client automatically attempts to reconnect (exponential backoff: 1s, 2s, 4s, ... up to 15s, max 10 attempts)
- Reconnection uses the session token from the original handshake — the player returns to their exact seat with hand and score preserved
- If all players disconnect for **5 minutes**, the room is destroyed
- If the lobby server goes down during a game, the game continues unaffected (lobby is only needed for login and matchmaking)

---

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| Client shows "Connecting to lobby..." forever | Lobby not running or wrong port | Verify `hh-lobby` is running on the port the client expects |
| "Failed to connect to game server" | Game server not running or not registered with lobby | Verify `hh-server` is running with the correct lobby address/port |
| Game doesn't start after 4 players join | One or more clients on different subphase | All 4 must be in the same room; check lobby logs for matchmaking output |
| "Unknown username" on login | Account doesn't exist on this lobby DB | Register first (happens automatically on first username entry) |
| LAN clients can't reach game server | Game server reports `127.0.0.1` to lobby | Apply the LAN workaround above (edit server_main.c) |
| "Protocol version mismatch" | Client and server built from different code | Rebuild all three binaries from the same source |
| Player reconnects to wrong seat | Session token mismatch | Shouldn't happen; file a bug if it does |

---

## Logs

All three binaries print status to stdout/stderr:

- **Lobby:** `[lobby-net]` prefixed messages — connections, logins, matchmaking, room creation
- **Game Server:** `[net]` prefixed messages — handshakes, room joins, game start/end
- **Client:** `[client_net]` and `[lobby-client]` prefixed messages — connection state, login flow

Example healthy startup sequence:
```
# Lobby
[lobby-net] Listening on port 7778
[lobby-net] Server registered: 127.0.0.1:7777 (16 rooms)

# Game Server
hh-server starting...
[net] Listening on port 7777
[lobby-link] Connected to lobby 127.0.0.1:7778

# Client
[lobby-client] Connecting to lobby 127.0.0.1:7778
[lobby-client] Connected to lobby
[lobby-client] Logging in as 'Alice'
[lobby-client] Sent login response
[lobby-client] Authenticated as 'Alice'
```
