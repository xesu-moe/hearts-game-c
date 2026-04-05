/* ============================================================
 * @deps-implements: lobby/server_registry.h
 * @deps-requires: lobby/server_registry.h (RegisteredServer),
 *                 lobby/db.h (LobbyDB, LobbyStmtID, lobbydb_stmt),
 *                 net/protocol.h (NetMsgServerRegister, NET_ADDR_LEN),
 *                 sqlite3.h, stdio.h, string.h
 * @deps-last-changed: 2026-03-25 — Step 18: Added heartbeat timeout
 * ============================================================ */

#include "server_registry.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ================================================================
 * File-scope state
 * ================================================================ */

static RegisteredServer g_servers[LOBBY_MAX_GAME_SERVERS];

/* ================================================================
 * Helpers
 * ================================================================ */

static double svreg_time_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void build_server_id(char *id, size_t id_len,
                            const char *addr, uint16_t port)
{
    snprintf(id, id_len, "%.60s:%d", addr, port);
}

static int find_slot_by_id(const char *id)
{
    for (int i = 0; i < LOBBY_MAX_GAME_SERVERS; i++) {
        if (g_servers[i].active && strcmp(g_servers[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_slot_by_conn(int conn_id)
{
    for (int i = 0; i < LOBBY_MAX_GAME_SERVERS; i++) {
        if (g_servers[i].active && g_servers[i].conn_id == conn_id) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void)
{
    for (int i = 0; i < LOBBY_MAX_GAME_SERVERS; i++) {
        if (!g_servers[i].active) return i;
    }
    return -1;
}

/* ================================================================
 * Public API
 * ================================================================ */

void svreg_init(void)
{
    memset(g_servers, 0, sizeof(g_servers));
    printf("[svreg] Initialized (max %d servers)\n", LOBBY_MAX_GAME_SERVERS);
}

int svreg_register(LobbyDB *ldb, int conn_id,
                   const NetMsgServerRegister *sr)
{
    char id[72];
    build_server_id(id, sizeof(id), sr->addr, sr->port);

    /* Check if already registered (reconnect case) */
    int idx = find_slot_by_id(id);
    if (idx < 0) {
        idx = find_free_slot();
        if (idx < 0) {
            fprintf(stderr, "[svreg] Registry full (%d servers)\n",
                    LOBBY_MAX_GAME_SERVERS);
            return -1;
        }
    }

    RegisteredServer *s = &g_servers[idx];
    s->active = true;
    strncpy(s->id, id, sizeof(s->id) - 1);
    s->id[sizeof(s->id) - 1] = '\0';
    strncpy(s->addr, sr->addr, NET_ADDR_LEN - 1);
    s->addr[NET_ADDR_LEN - 1] = '\0';
    s->port = sr->port;
    s->max_rooms = sr->max_rooms;
    s->current_rooms = sr->current_rooms;
    s->conn_id = conn_id;
    s->last_heartbeat = svreg_time_now();

    /* Persist to DB */
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_UPSERT_SERVER);
    if (stmt) {
        sqlite3_bind_text(stmt, 1, s->id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, s->addr, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, s->port);
        sqlite3_bind_int(stmt, 4, s->max_rooms);
        sqlite3_bind_int(stmt, 5, s->current_rooms);
        sqlite3_step(stmt);
    }

    printf("[svreg] Registered server '%s' (conn %d, capacity %d/%d)\n",
           id, conn_id, s->current_rooms, s->max_rooms);
    return idx;
}

void svreg_unregister(LobbyDB *ldb, int conn_id)
{
    int idx = find_slot_by_conn(conn_id);
    if (idx < 0) return;

    RegisteredServer *s = &g_servers[idx];
    printf("[svreg] Unregistered server '%s' (conn %d)\n", s->id, conn_id);

    /* Remove from DB */
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_DELETE_SERVER);
    if (stmt) {
        sqlite3_bind_text(stmt, 1, s->id, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }

    memset(s, 0, sizeof(*s));
}

void svreg_heartbeat(int conn_id, const NetMsgServerHeartbeat *hb)
{
    int idx = find_slot_by_conn(conn_id);
    if (idx < 0) return;

    g_servers[idx].current_rooms = hb->current_rooms;
    g_servers[idx].last_heartbeat = svreg_time_now();
}

const RegisteredServer *svreg_pick_server(void)
{
    const RegisteredServer *best = NULL;
    int best_available = -1;

    for (int i = 0; i < LOBBY_MAX_GAME_SERVERS; i++) {
        if (!g_servers[i].active) continue;
        int available = (int)g_servers[i].max_rooms - (int)g_servers[i].current_rooms;
        if (available <= 0) continue;
        if (!best || available > best_available) {
            best = &g_servers[i];
            best_available = available;
        }
    }
    return best;
}

const RegisteredServer *svreg_find_by_conn(int conn_id)
{
    int idx = find_slot_by_conn(conn_id);
    if (idx < 0) return NULL;
    return &g_servers[idx];
}

int svreg_count(void)
{
    int count = 0;
    for (int i = 0; i < LOBBY_MAX_GAME_SERVERS; i++) {
        if (g_servers[i].active) count++;
    }
    return count;
}

void svreg_expire_dead(LobbyDB *ldb, double now, double timeout,
                       SvregDeadCb on_dead)
{
    for (int i = 0; i < LOBBY_MAX_GAME_SERVERS; i++) {
        if (!g_servers[i].active) continue;
        if (now - g_servers[i].last_heartbeat > timeout) {
            printf("[svreg] Server '%s' timed out (%.0fs since heartbeat)\n",
                   g_servers[i].id,
                   now - g_servers[i].last_heartbeat);
            int dead_conn = g_servers[i].conn_id;
            svreg_unregister(ldb, dead_conn);
            if (on_dead) on_dead(dead_conn);
        }
    }
}
