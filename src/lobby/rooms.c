/* ============================================================
 * @deps-implements: lobby/rooms.h
 * @deps-requires: lobby/rooms.h (PendingRoomRequest),
 *                 lobby/db.h (LobbyDB, LobbyStmtID, lobbydb_stmt),
 *                 sqlite3.h, stdio.h, string.h, sys/random.h
 * @deps-last-changed: 2026-03-24 — Step 16: Room Code System
 * ============================================================ */

#include "rooms.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

/* ================================================================
 * Room Code Generation
 * ================================================================ */

static const char ROOM_CODE_CHARS[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
#define ROOM_CODE_ALPHABET_SIZE (int)(sizeof(ROOM_CODE_CHARS) - 1)

bool lobby_rooms_generate_code(LobbyDB *ldb, char out[LOBBY_ROOM_CODE_LEN])
{
    uint8_t rand_bytes[4];

    for (int attempt = 0; attempt < 20; attempt++) {
        {
            ssize_t got = 0, total = 0;
            while (total < (ssize_t)sizeof(rand_bytes)) {
                got = getrandom(rand_bytes + total,
                                sizeof(rand_bytes) - (size_t)total, 0);
                if (got < 0) break;
                total += got;
            }
            if (total < (ssize_t)sizeof(rand_bytes)) {
                /* Fallback to less random source */
                for (int i = 0; i < 4; i++)
                    rand_bytes[i] = (uint8_t)(rand() % 256);
            }
        }

        for (int i = 0; i < 4; i++) {
            out[i] = ROOM_CODE_CHARS[rand_bytes[i] % ROOM_CODE_ALPHABET_SIZE];
        }
        out[4] = '\0';

        /* Check DB for collision */
        char addr[NET_ADDR_LEN];
        uint16_t port;
        char status[16];
        if (lobby_rooms_lookup(ldb, out, addr, &port, status)) {
            continue; /* code exists in DB */
        }

        /* Check pending array for collision */
        if (lobby_pending_find_by_code(out) >= 0) {
            continue; /* code in pending */
        }

        return true; /* unique code found */
    }

    fprintf(stderr, "[rooms] Failed to generate unique code after 20 attempts\n");
    return false;
}

/* ================================================================
 * Room Code DB Operations
 * ================================================================ */

bool lobby_rooms_insert(LobbyDB *ldb, const char *code,
                        const char *server_addr, uint16_t server_port)
{
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_INSERT_ROOM);
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, code, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, server_addr, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, server_port);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[rooms] Insert room '%s' failed: %s\n",
                code, sqlite3_errmsg(lobbydb_handle(ldb)));
        return false;
    }
    return true;
}

bool lobby_rooms_lookup(LobbyDB *ldb, const char *code,
                        char addr_out[NET_ADDR_LEN], uint16_t *port_out,
                        char status_out[16])
{
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_FIND_ROOM);
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, code, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        return false;
    }

    const char *addr = (const char *)sqlite3_column_text(stmt, 0);
    if (addr) {
        strncpy(addr_out, addr, NET_ADDR_LEN - 1);
        addr_out[NET_ADDR_LEN - 1] = '\0';
    }
    *port_out = (uint16_t)sqlite3_column_int(stmt, 1);
    const char *status = (const char *)sqlite3_column_text(stmt, 2);
    if (status && status_out) {
        strncpy(status_out, status, 15);
        status_out[15] = '\0';
    }
    return true;
}

bool lobby_rooms_set_status(LobbyDB *ldb, const char *code,
                            const char *status)
{
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_UPDATE_ROOM_STATUS);
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, status, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, code, -1, SQLITE_STATIC);
    return sqlite3_step(stmt) == SQLITE_DONE;
}

void lobby_rooms_cleanup(LobbyDB *ldb)
{
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_CLEANUP_ROOMS);
    if (!stmt) return;
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        int changes = sqlite3_changes(lobbydb_handle(ldb));
        if (changes > 0) {
            printf("[rooms] Cleaned up %d expired room codes\n", changes);
        }
    }
}

/* ================================================================
 * Pending Room Requests
 * ================================================================ */

static PendingRoomRequest g_pending[LOBBY_MAX_PENDING];

void lobby_pending_init(void)
{
    memset(g_pending, 0, sizeof(g_pending));
}

int lobby_pending_add(const char *code, int client_conn, int server_conn,
                      double now)
{
    for (int i = 0; i < LOBBY_MAX_PENDING; i++) {
        if (!g_pending[i].active) {
            g_pending[i].active = true;
            strncpy(g_pending[i].room_code, code, NET_ROOM_CODE_LEN - 1);
            g_pending[i].room_code[NET_ROOM_CODE_LEN - 1] = '\0';
            g_pending[i].client_conn_id = client_conn;
            g_pending[i].server_conn_id = server_conn;
            g_pending[i].created_at = now;
            return i;
        }
    }
    fprintf(stderr, "[rooms] Pending array full (%d)\n", LOBBY_MAX_PENDING);
    return -1;
}

int lobby_pending_find_by_code(const char *code)
{
    for (int i = 0; i < LOBBY_MAX_PENDING; i++) {
        if (g_pending[i].active &&
            strncmp(g_pending[i].room_code, code, NET_ROOM_CODE_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

void lobby_pending_remove(int idx)
{
    if (idx >= 0 && idx < LOBBY_MAX_PENDING) {
        g_pending[idx].active = false;
    }
}

void lobby_pending_remove_by_client(int conn_id)
{
    for (int i = 0; i < LOBBY_MAX_PENDING; i++) {
        if (g_pending[i].active && g_pending[i].client_conn_id == conn_id) {
            g_pending[i].active = false;
        }
    }
}

void lobby_pending_remove_by_server(int conn_id)
{
    for (int i = 0; i < LOBBY_MAX_PENDING; i++) {
        if (g_pending[i].active && g_pending[i].server_conn_id == conn_id) {
            g_pending[i].active = false;
        }
    }
}

void lobby_pending_expire(double now, PendingTimeoutCb on_timeout)
{
    for (int i = 0; i < LOBBY_MAX_PENDING; i++) {
        if (g_pending[i].active &&
            (now - g_pending[i].created_at) > LOBBY_PENDING_TIMEOUT) {
            printf("[rooms] Pending request '%s' timed out (%.1fs)\n",
                   g_pending[i].room_code,
                   now - g_pending[i].created_at);
            if (on_timeout) {
                on_timeout(g_pending[i].client_conn_id);
            }
            g_pending[i].active = false;
        }
    }
}

const PendingRoomRequest *lobby_pending_get(int idx)
{
    if (idx >= 0 && idx < LOBBY_MAX_PENDING && g_pending[idx].active) {
        return &g_pending[idx];
    }
    return NULL;
}
