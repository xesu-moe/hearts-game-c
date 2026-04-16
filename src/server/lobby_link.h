/* ============================================================
 * Game Server — Lobby Link
 *
 * Manages the game server's outbound TCP connection to the
 * lobby server.  Registers on connect, sends heartbeats,
 * handles room creation requests from the lobby.
 *
 * @deps-exports: lobby_link_init, lobby_link_connect,
 *                lobby_link_update, lobby_link_shutdown,
 *                lobby_link_is_connected, lobby_link_send_result,
 *                lobby_link_notify_room_destroyed
 * @deps-requires: net/protocol.h (NET_MAX_PLAYERS, NET_AUTH_TOKEN_LEN, NET_MSG_SERVER_ROOM_DESTROYED)
 * @deps-used-by: server/server_main.c, server/room.c
 * @deps-last-changed: 2026-03-27 — Step 23: Added lobby_link_notify_room_destroyed() to notify lobby when all players leave room
 * ============================================================ */

#ifndef LOBBY_LINK_H
#define LOBBY_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "net/protocol.h"

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

/* Report game completion to lobby. Called from room.c when game finishes.
 * Sends final scores, winners, rounds played, player auth tokens, and
 * per-player stat counters so the lobby can record match history.
 * Silently drops if not connected. */
typedef struct ServerGame ServerGame;
void lobby_link_send_result(const char *room_code,
                            const int16_t scores[NET_MAX_PLAYERS],
                            const uint8_t winner_seats[NET_MAX_PLAYERS],
                            int winner_count,
                            int rounds_played,
                            const uint8_t player_tokens[][NET_AUTH_TOKEN_LEN],
                            const ServerGame *sg);

/* Notify lobby that a waiting room was destroyed (all players left).
 * Silently drops if not connected. */
void lobby_link_notify_room_destroyed(const char *room_code);

#endif /* LOBBY_LINK_H */
