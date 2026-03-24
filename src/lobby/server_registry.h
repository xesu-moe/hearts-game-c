/* ============================================================
 * Lobby Server — Game Server Registry
 *
 * Tracks active game servers (in-memory + DB). Game servers
 * connect to the lobby and register; the lobby picks the
 * lowest-load server when creating rooms.
 *
 * @deps-exports: RegisteredServer, svreg_init, svreg_register,
 *                svreg_unregister, svreg_heartbeat, svreg_pick_server,
 *                svreg_find_by_conn, svreg_count
 * @deps-requires: lobby/db.h (LobbyDB), net/protocol.h (NetMsgServerRegister,
 *                 NetMsgServerHeartbeat, NET_ADDR_LEN)
 * @deps-used-by: lobby/lobby_net.c
 * @deps-last-changed: 2026-03-24 — Step 16: Server Registry
 * ============================================================ */

#ifndef LOBBY_SERVER_REGISTRY_H
#define LOBBY_SERVER_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>

#include "db.h"
#include "net/protocol.h"

#define LOBBY_MAX_GAME_SERVERS 32

typedef struct RegisteredServer {
    bool     active;
    char     id[72];              /* "addr:port" */
    char     addr[NET_ADDR_LEN];
    uint16_t port;
    uint16_t max_rooms;
    uint16_t current_rooms;
    int      conn_id;             /* for sending messages via lobby's NetSocket */
    double   last_heartbeat;
} RegisteredServer;

/* Initialize the server registry. */
void svreg_init(void);

/* Register a game server (or update if same addr:port).
 * Returns index in registry, or -1 on failure. */
int svreg_register(LobbyDB *ldb, int conn_id,
                   const NetMsgServerRegister *sr);

/* Unregister a game server by connection ID (on disconnect). */
void svreg_unregister(LobbyDB *ldb, int conn_id);

/* Update heartbeat data for a game server. */
void svreg_heartbeat(int conn_id, const NetMsgServerHeartbeat *hb);

/* Pick the game server with the most available capacity.
 * Returns NULL if no servers have capacity. */
const RegisteredServer *svreg_pick_server(void);

/* Find a registered server by its connection ID. */
const RegisteredServer *svreg_find_by_conn(int conn_id);

/* Return the number of active registered servers. */
int svreg_count(void);

#endif /* LOBBY_SERVER_REGISTRY_H */
