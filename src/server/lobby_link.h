/* ============================================================
 * Game Server — Lobby Link
 *
 * Manages the game server's outbound TCP connection to the
 * lobby server.  Registers on connect, sends heartbeats,
 * handles room creation requests from the lobby.
 *
 * @deps-exports: lobby_link_init, lobby_link_connect,
 *                lobby_link_update, lobby_link_shutdown,
 *                lobby_link_is_connected
 * @deps-requires: (none — forward-declares only)
 * @deps-used-by: server/server_main.c
 * @deps-last-changed: 2026-03-24 — Step 16: Room Code System
 * ============================================================ */

#ifndef LOBBY_LINK_H
#define LOBBY_LINK_H

#include <stdbool.h>
#include <stdint.h>

/* Initialize the lobby link with connection parameters.
 * Does not connect yet — call lobby_link_connect() after. */
void lobby_link_init(const char *lobby_addr, uint16_t lobby_port,
                     const char *self_addr, uint16_t self_port,
                     uint16_t max_rooms);

/* Attempt to connect to the lobby server. Returns true on success.
 * Non-blocking — may need multiple lobby_link_update() calls to complete. */
bool lobby_link_connect(void);

/* Per-tick update: poll connection, handle incoming messages,
 * send heartbeat, attempt reconnect if disconnected. */
void lobby_link_update(void);

/* Shut down the lobby link and close connection. */
void lobby_link_shutdown(void);

/* Check if currently connected to lobby. */
bool lobby_link_is_connected(void);

#endif /* LOBBY_LINK_H */
