/* ============================================================
 * @deps-exports: server_net_init(), server_net_shutdown(),
 *                server_net_listen(), server_net_update()
 * @deps-requires: stdbool.h, stdint.h
 * @deps-last-changed: 2026-03-23 — Step 6: Server Network Loop
 * ============================================================ */

#ifndef SERVER_NET_H
#define SERVER_NET_H

#include <stdbool.h>
#include <stdint.h>

/* Initialize the server network layer.
 * max_connections: max simultaneous client connections. */
void server_net_init(int max_connections);

/* Shut down: close all connections, free resources. */
void server_net_shutdown(void);

/* Start listening on the given port. Returns true on success. */
bool server_net_listen(uint16_t port);

/* Called once per tick in the main loop.
 * Accepts new connections, receives and routes messages,
 * ticks all rooms, broadcasts state updates, handles disconnects. */
void server_net_update(void);

#endif /* SERVER_NET_H */
