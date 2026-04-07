# Hollow Hearts - Docker Deployment Guide

Deploy the lobby server and game server using Docker.

## Architecture

```
Clients  -->  hh-lobby (:7778)  -->  hh-server (:7777)
              (matchmaking,          (game rooms,
               accounts, rooms)       authoritative logic)
              [SQLite DB]
```

- **hh-lobby** handles authentication, matchmaking, and room codes. Stores data in a SQLite file.
- **hh-server** runs the actual card game. Registers itself with the lobby and receives player assignments.
- Clients connect to the lobby first. The lobby tells them which game server to join.

## Dockerfile

```dockerfile
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc make libc6-dev libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .
RUN make server lobby

# --- Runtime ---
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /build/hh-server /app/
COPY --from=build /build/hh-lobby /app/

VOLUME /data
EXPOSE 7777 7778
CMD ["/bin/sh", "-c", "/app/hh-lobby & exec /app/hh-server"]
```

## docker-compose.yml

```yaml
services:
  lobby:
    build: .
    command: /app/hh-lobby 7778 /data/lobby.db
    ports:
      - "7778:7778"
    volumes:
      - lobby-data:/data
    restart: unless-stopped
    healthcheck:
      test: ["CMD-SHELL", "ss -tlnp | grep -q 7778"]
      interval: 10s
      timeout: 3s
      retries: 3

  server:
    build: .
    command: /app/hh-server 7777 lobby 7778 ${PUBLIC_ADDR:-127.0.0.1}
    ports:
      - "7777:7777"
    depends_on:
      lobby:
        condition: service_healthy
    restart: unless-stopped
    healthcheck:
      test: ["CMD-SHELL", "ss -tlnp | grep -q 7777"]
      interval: 10s
      timeout: 3s
      retries: 3

volumes:
  lobby-data:
```

## Configuration

### hh-lobby

```
hh-lobby [port] [db_path]
```

| Arg | Default | Description |
|-----|---------|-------------|
| `port` | `9999` | TCP port to listen on |
| `db_path` | `lobby.db` | SQLite database file (auto-created on first run) |

The database directory must be writable. SQLite WAL mode creates additional `*.db-wal` and `*.db-shm` files alongside the main database.

### hh-server

```
hh-server [port] [lobby_addr lobby_port] [public_addr]
```

| Arg | Default | Description |
|-----|---------|-------------|
| `port` | `7777` | TCP port to listen on |
| `lobby_addr` | *(none)* | Lobby server address (hostname or IP) |
| `lobby_port` | *(none)* | Lobby server port |
| `public_addr` | `127.0.0.1` | Address advertised to clients via the lobby |

`public_addr` is what clients receive when the lobby assigns them to this server. Set it to your server's public IP or DNS name.

## Deployment Steps

### 1. Set your public address

Create a `.env` file next to `docker-compose.yml`:

```env
PUBLIC_ADDR=your-server-ip-or-domain
```

This is the address clients will use to connect to the game server. If lobby and server are on the same machine, clients connect to the lobby address and the lobby tells them where the game server is.

### 2. Build and start

```bash
docker compose up -d --build
```

### 3. Verify

```bash
# Check both services are running
docker compose ps

# Check logs
docker compose logs lobby
docker compose logs server

# You should see:
#   [lobby] Listening on port 7778
#   [server] Listening on port 7777
#   [server] Registered with lobby at lobby:7778
```

### 4. Point the client

Either change `DEFAULT_LOBBY_ADDR` in `src/main.c` to your DNS name, or run:

```bash
./hollow-hearts --lobby-addr your-server-ip-or-domain --lobby-port 7778
```

## Multiple Game Servers

The lobby supports multiple game servers with load balancing. Add more server instances:

```yaml
  server2:
    build: .
    command: /app/hh-server 7777 lobby 7778 ${PUBLIC_ADDR_2:-127.0.0.1}
    ports:
      - "7779:7777"
    depends_on:
      lobby:
        condition: service_healthy
    restart: unless-stopped
```

Each server registers with the lobby and sends heartbeats every 15 seconds. The lobby assigns new games to the least-loaded server.

## Persistent Data

The lobby database (`/data/lobby.db`) stores:
- Player accounts and sessions
- Match history and stats (ELO ratings)
- Active room codes

Back up the volume regularly:

```bash
docker compose exec lobby sqlite3 /data/lobby.db ".backup /data/backup.db"
docker cp $(docker compose ps -q lobby):/data/backup.db ./lobby-backup.db
```

## Firewall

Open these ports for client connections:

| Port | Protocol | Service |
|------|----------|---------|
| 7778 | TCP | Lobby (authentication, matchmaking) |
| 7777 | TCP | Game server (gameplay) |

The lobby-to-server communication happens over Docker's internal network (`lobby:7778`) and does not need external exposure.

## Troubleshooting

**Server can't reach lobby**: The `depends_on` with `service_healthy` ensures the lobby is listening before the server starts. If it still fails, check that the lobby healthcheck is passing with `docker compose ps`.

**Clients can't connect**: Verify `PUBLIC_ADDR` is set to an address reachable by clients (not `127.0.0.1` for remote play). Check firewall rules for ports 7777 and 7778.

**Database locked errors**: Only one lobby instance should write to the database. Don't run multiple lobby containers against the same volume.

**Server not appearing in lobby**: Check server logs for `Registered with lobby` message. The server sends heartbeats every 15s; if the lobby doesn't receive them, it marks the server as dead.
