/* ============================================================
 * Lobby Server — Network Layer
 *
 * Accepts TCP connections from clients and game servers on a
 * single listener.  Routes incoming messages to stub handlers
 * (real logic added in Steps 15-18).
 *
 * @deps-exports: lobby_net_init, lobby_net_shutdown,
 *                lobby_net_listen, lobby_net_update
 * @deps-requires: lobby/db.h (LobbyDB — forward-declared)
 * @deps-used-by: lobby/lobby_main.c
 * @deps-last-changed: 2026-03-24 — Step 14: Lobby Server Foundation
 * ============================================================ */

#ifndef LOBBY_NET_H
#define LOBBY_NET_H

#include <stdbool.h>
#include <stdint.h>

/* Forward declare — lobby_net stores a borrowed pointer, not owned */
struct LobbyDB;

/* Initialize the lobby network layer.
 * max_connections: max simultaneous TCP connections.
 * ldb: opened LobbyDB (non-owning reference). */
void lobby_net_init(int max_connections, struct LobbyDB *ldb);

/* Shut down: close all connections, free per-connection state. */
void lobby_net_shutdown(void);

/* Bind and listen on port. Returns true on success. */
bool lobby_net_listen(uint16_t port);

/* Per-tick update: poll, accept, receive, route, cleanup. */
void lobby_net_update(void);

#endif /* LOBBY_NET_H */
