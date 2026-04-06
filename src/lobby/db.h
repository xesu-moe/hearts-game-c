/* ============================================================
 * @deps-exports: LobbyDB, LobbyStmtID, lobbydb_open, lobbydb_close,
 *                lobbydb_stmt, lobbydb_handle
 * @deps-requires: (none — forward-declares sqlite3 types)
 * @deps-used-by: lobby/lobby_main.c, lobby/lobby_net.c, lobby/auth.c, lobby/rooms.c, lobby/server_registry.c
 * @deps-last-changed: 2026-03-26 — Step 21: Added LOBBY_STMT_UPDATE_ELO for ELO updates
 * ============================================================ */

#ifndef LOBBY_DB_H
#define LOBBY_DB_H

#include <stdbool.h>
#include <stdint.h>

/* Forward-declare sqlite3 types so consumers don't need <sqlite3.h> */
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

/* ================================================================
 * Prepared Statement IDs
 * ================================================================ */

typedef enum LobbyStmtID {
    /* Auth (Step 15) */
    LOBBY_STMT_REGISTER_ACCOUNT,
    LOBBY_STMT_FIND_BY_USERNAME,
    LOBBY_STMT_FIND_BY_PUBKEY,
    LOBBY_STMT_UPDATE_LAST_LOGIN,
    LOBBY_STMT_CREATE_SESSION,
    LOBBY_STMT_DELETE_SESSION,
    LOBBY_STMT_DELETE_EXPIRED,
    LOBBY_STMT_FIND_SESSION,
    LOBBY_STMT_INIT_STATS,
    LOBBY_STMT_GET_STATS,
    /* Rooms (Step 16) */
    LOBBY_STMT_INSERT_ROOM,
    LOBBY_STMT_FIND_ROOM,
    LOBBY_STMT_UPDATE_ROOM_STATUS,
    LOBBY_STMT_CLEANUP_ROOMS,
    /* Server Registry (Step 16) */
    LOBBY_STMT_UPSERT_SERVER,
    LOBBY_STMT_DELETE_SERVER,
    LOBBY_STMT_UPDATE_HEARTBEAT,
    LOBBY_STMT_PICK_SERVER,
    /* Match recording (Step 18) */
    LOBBY_STMT_INSERT_MATCH,
    LOBBY_STMT_INSERT_MATCH_PLAYER,
    LOBBY_STMT_UPDATE_STATS,
    /* Username change (Step 19) */
    LOBBY_STMT_CHANGE_USERNAME,
    /* ELO update (Step 21) */
    LOBBY_STMT_UPDATE_ELO,
    /* Stats & Leaderboard queries */
    LOBBY_STMT_GET_FULL_STATS,
    LOBBY_STMT_UPDATE_FULL_STATS,
    LOBBY_STMT_GET_LEADERBOARD,
    LOBBY_STMT_GET_PLAYER_RANK,
    LOBBY_STMT_GET_BEST_WORST_SCORE,
    LOBBY_STMT_GET_AVG_PLACEMENT,
    /* Friend system (migration v8) */
    LOBBY_STMT_FRIEND_SEARCH,
    LOBBY_STMT_FRIEND_COUNT,
    LOBBY_STMT_FRIEND_LIST,
    LOBBY_STMT_FRIEND_INSERT,
    LOBBY_STMT_FRIEND_DELETE,
    LOBBY_STMT_FRIEND_CHECK,
    LOBBY_STMT_FREQ_INSERT,
    LOBBY_STMT_FREQ_DELETE,
    LOBBY_STMT_FREQ_LIST_INCOMING,
    LOBBY_STMT_FREQ_LIST_OUTGOING,
    LOBBY_STMT_FREQ_CHECK,
    LOBBY_STMT_FREQ_COUNT_OUTGOING,
    LOBBY_STMT_FBLOCK_INSERT,
    LOBBY_STMT_FBLOCK_CHECK,
    LOBBY_STMT_GET_USERNAME,
    LOBBY_STMT__COUNT /* must be last */
} LobbyStmtID;

/* ================================================================
 * Database Handle
 * ================================================================ */

typedef struct LobbyDB {
    sqlite3      *db;
    sqlite3_stmt *stmts[LOBBY_STMT__COUNT];
    bool          open;
} LobbyDB;

/* Open (or create) the database at path.
 * Sets PRAGMAs, runs pending migrations, prepares cached statements.
 * Returns true on success, false on failure (logged to stderr). */
bool lobbydb_open(LobbyDB *ldb, const char *path);

/* Finalize all prepared statements and close the database. */
void lobbydb_close(LobbyDB *ldb);

/* Return a reset+cleared prepared statement for fresh binding.
 * Returns NULL if id is out of range or on error. */
sqlite3_stmt *lobbydb_stmt(LobbyDB *ldb, LobbyStmtID id);

/* Direct access to the raw sqlite3 handle (for ad-hoc queries). */
sqlite3 *lobbydb_handle(LobbyDB *ldb);

#endif /* LOBBY_DB_H */
