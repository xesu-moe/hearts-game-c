/* ============================================================
 * Lobby Server — Room Code System
 *
 * Generates 4-char room codes, manages room code lifecycle in
 * the database, and tracks pending create-room requests waiting
 * for game server ACK.
 *
 * @deps-exports: PendingRoomRequest, lobby_rooms_generate_code,
 *                lobby_rooms_insert, lobby_rooms_lookup,
 *                lobby_rooms_set_status, lobby_rooms_cleanup,
 *                lobby_pending_*
 * @deps-requires: lobby/db.h (LobbyDB), net/protocol.h (NET_*)
 * @deps-used-by: lobby/lobby_net.c
 * @deps-last-changed: 2026-03-24 — Step 16: Room Code System
 * ============================================================ */

#ifndef LOBBY_ROOMS_H
#define LOBBY_ROOMS_H

#include <stdbool.h>
#include <stdint.h>

#include "db.h"
#include "net/protocol.h"

/* ================================================================
 * Constants
 * ================================================================ */

#define LOBBY_ROOM_CODE_LEN    5    /* 4 chars + NUL */
#define LOBBY_ROOM_EXPIRY_SEC  600  /* 10 minutes */
#define LOBBY_MAX_PENDING      64
#define LOBBY_PENDING_TIMEOUT  15.0 /* seconds to wait for server ACK */

/* ================================================================
 * Room Code DB Operations
 * ================================================================ */

/* Generate a unique 4-char room code.
 * Checks DB and pending array for collisions. Returns true on success. */
bool lobby_rooms_generate_code(LobbyDB *ldb, char out[LOBBY_ROOM_CODE_LEN]);

/* Insert a room code into the database with 10-min expiry. */
bool lobby_rooms_insert(LobbyDB *ldb, const char *code,
                        const char *server_addr, uint16_t server_port);

/* Look up a room code. Returns true if found.
 * Writes server address/port and status. */
bool lobby_rooms_lookup(LobbyDB *ldb, const char *code,
                        char addr_out[NET_ADDR_LEN], uint16_t *port_out,
                        char status_out[16]);

/* Update room code status ('active', 'playing', 'finished', 'expired'). */
bool lobby_rooms_set_status(LobbyDB *ldb, const char *code,
                            const char *status);

/* Delete expired room codes from the database. */
void lobby_rooms_cleanup(LobbyDB *ldb);

/* ================================================================
 * Pending Room Requests (in-memory, for ACK model)
 * ================================================================ */

typedef struct PendingRoomRequest {
    bool   active;
    char   room_code[NET_ROOM_CODE_LEN];
    int    client_conn_id;
    int    server_conn_id;
    double created_at;
} PendingRoomRequest;

void lobby_pending_init(void);
int  lobby_pending_add(const char *code, int client_conn, int server_conn,
                       double now);
int  lobby_pending_find_by_code(const char *code);
void lobby_pending_remove(int idx);
void lobby_pending_remove_by_client(int conn_id);
void lobby_pending_remove_by_server(int conn_id);

/* Expire stale pending requests. Calls on_timeout for each expired entry. */
typedef void (*PendingTimeoutCb)(int client_conn_id);
void lobby_pending_expire(double now, PendingTimeoutCb on_timeout);

const PendingRoomRequest *lobby_pending_get(int idx);

#endif /* LOBBY_ROOMS_H */
