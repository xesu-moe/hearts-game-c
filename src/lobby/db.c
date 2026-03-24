/* ============================================================
 * @deps-implements: lobby/db.h
 * @deps-requires: lobby/db.h (LobbyDB, LobbyStmtID),
 *                 sqlite3.h, stdio.h, string.h
 * @deps-last-changed: 2026-03-24 — Step 15: Auth prepared statements
 * ============================================================ */

#include "db.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

/* ================================================================
 * Schema Version (via PRAGMA user_version)
 * ================================================================ */

#define LOBBY_SCHEMA_VERSION 6

/* ================================================================
 * Migrations
 * ================================================================ */

typedef struct {
    int         version;
    const char *sql;
    const char *desc;
} Migration;

static const Migration MIGRATIONS[] = {
    {1,
     "CREATE TABLE accounts ("
     "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
     "  username   TEXT NOT NULL UNIQUE COLLATE NOCASE,"
     "  public_key BLOB NOT NULL UNIQUE,"
     "  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
     "  last_login INTEGER NOT NULL DEFAULT 0"
     ");"
     "CREATE INDEX idx_accounts_username ON accounts(username COLLATE NOCASE);",
     "accounts table (Ed25519 public key auth)"},

    {2,
     "CREATE TABLE sessions ("
     "  token      BLOB PRIMARY KEY,"
     "  account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
     "  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
     "  expires_at INTEGER NOT NULL"
     ");"
     "CREATE INDEX idx_sessions_account ON sessions(account_id);"
     "CREATE INDEX idx_sessions_expires ON sessions(expires_at);",
     "sessions table"},

    {3,
     "CREATE TABLE match_history ("
     "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
     "  room_code     TEXT NOT NULL,"
     "  played_at     INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
     "  rounds_played INTEGER NOT NULL,"
     "  duration_secs INTEGER NOT NULL DEFAULT 0"
     ");"
     "CREATE TABLE match_players ("
     "  match_id   INTEGER NOT NULL REFERENCES match_history(id) ON DELETE CASCADE,"
     "  account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
     "  seat       INTEGER NOT NULL CHECK(seat >= 0 AND seat <= 3),"
     "  final_score INTEGER NOT NULL DEFAULT 0,"
     "  placement  INTEGER NOT NULL DEFAULT 0,"
     "  PRIMARY KEY (match_id, account_id)"
     ");"
     "CREATE INDEX idx_match_players_account ON match_players(account_id);",
     "match_history + match_players tables"},

    {4,
     "CREATE TABLE stats ("
     "  account_id   INTEGER PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,"
     "  games_played INTEGER NOT NULL DEFAULT 0,"
     "  games_won    INTEGER NOT NULL DEFAULT 0,"
     "  total_score  INTEGER NOT NULL DEFAULT 0,"
     "  elo_rating   REAL NOT NULL DEFAULT 1000.0"
     ");",
     "stats table"},

    {5,
     "CREATE TABLE room_codes ("
     "  code        TEXT PRIMARY KEY,"
     "  server_addr TEXT NOT NULL,"
     "  server_port INTEGER NOT NULL,"
     "  created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
     "  expires_at  INTEGER NOT NULL,"
     "  status      TEXT NOT NULL DEFAULT 'active'"
     "    CHECK(status IN ('active','playing','finished','expired'))"
     ");"
     "CREATE INDEX idx_room_codes_expires ON room_codes(expires_at);"
     "CREATE INDEX idx_room_codes_status ON room_codes(status);",
     "room_codes table"},

    {6,
     "CREATE TABLE game_servers ("
     "  id             TEXT PRIMARY KEY,"
     "  addr           TEXT NOT NULL,"
     "  port           INTEGER NOT NULL,"
     "  max_rooms      INTEGER NOT NULL,"
     "  current_rooms  INTEGER NOT NULL DEFAULT 0,"
     "  registered_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
     "  last_heartbeat INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
     "  UNIQUE(addr, port)"
     ");"
     "CREATE INDEX idx_game_servers_load ON game_servers(current_rooms);",
     "game_servers table"},
};

#define MIGRATION_COUNT (int)(sizeof(MIGRATIONS) / sizeof(MIGRATIONS[0]))

/* ================================================================
 * Internal: Run a single SQL string (may contain multiple statements)
 * ================================================================ */

static int lobbydb_exec(sqlite3 *db, const char *sql, const char *context)
{
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] %s failed: %s\n", context, err ? err : "unknown");
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

/* ================================================================
 * Internal: Get current schema version via PRAGMA user_version
 * ================================================================ */

static int lobbydb_get_version(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] Failed to query user_version: %s\n",
                sqlite3_errmsg(db));
        return -1;
    }
    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

/* ================================================================
 * Internal: Set schema version via PRAGMA user_version
 * ================================================================ */

static int lobbydb_set_version(sqlite3 *db, int version)
{
    char sql[64];
    snprintf(sql, sizeof(sql), "PRAGMA user_version = %d;", version);
    return lobbydb_exec(db, sql, "set user_version");
}

/* ================================================================
 * Internal: Apply pending migrations
 * ================================================================ */

static int lobbydb_migrate(sqlite3 *db)
{
    int current = lobbydb_get_version(db);
    if (current < 0) return -1;

    if (current >= LOBBY_SCHEMA_VERSION) {
        printf("[db] Schema up to date (version %d)\n", current);
        return 0;
    }

    printf("[db] Migrating from version %d to %d\n", current, LOBBY_SCHEMA_VERSION);

    for (int i = 0; i < MIGRATION_COUNT; i++) {
        if (MIGRATIONS[i].version <= current) continue;

        printf("[db]   V%d: %s\n", MIGRATIONS[i].version, MIGRATIONS[i].desc);

        if (lobbydb_exec(db, "BEGIN;", "begin migration") < 0) return -1;
        if (lobbydb_exec(db, MIGRATIONS[i].sql, MIGRATIONS[i].desc) < 0) {
            lobbydb_exec(db, "ROLLBACK;", "rollback migration");
            return -1;
        }
        if (lobbydb_exec(db, "COMMIT;", "commit migration") < 0) return -1;

        /* PRAGMA user_version is not transactional — set after commit */
        if (lobbydb_set_version(db, MIGRATIONS[i].version) < 0) return -1;
    }

    printf("[db] Migration complete (version %d)\n", LOBBY_SCHEMA_VERSION);
    return 0;
}

/* ================================================================
 * Prepared Statement Cache
 * ================================================================ */

static const char *LOBBY_STMT_SQL[LOBBY_STMT__COUNT] = {
    [LOBBY_STMT_REGISTER_ACCOUNT] =
        "INSERT INTO accounts (username, public_key) VALUES (?, ?)",
    [LOBBY_STMT_FIND_BY_USERNAME] =
        "SELECT id, public_key FROM accounts WHERE username = ? COLLATE NOCASE",
    [LOBBY_STMT_FIND_BY_PUBKEY] =
        "SELECT id FROM accounts WHERE public_key = ?",
    [LOBBY_STMT_UPDATE_LAST_LOGIN] =
        "UPDATE accounts SET last_login = strftime('%s','now') WHERE id = ?",
    [LOBBY_STMT_CREATE_SESSION] =
        "INSERT INTO sessions (token, account_id, expires_at) "
        "VALUES (?, ?, strftime('%s','now') + 86400)",
    [LOBBY_STMT_DELETE_SESSION] =
        "DELETE FROM sessions WHERE token = ?",
    [LOBBY_STMT_DELETE_EXPIRED] =
        "DELETE FROM sessions WHERE expires_at < strftime('%s','now')",
    [LOBBY_STMT_FIND_SESSION] =
        "SELECT account_id FROM sessions WHERE token = ? "
        "AND expires_at > strftime('%s','now')",
    [LOBBY_STMT_INIT_STATS] =
        "INSERT OR IGNORE INTO stats (account_id) VALUES (?)",
    [LOBBY_STMT_GET_STATS] =
        "SELECT games_played, games_won, CAST(elo_rating AS INTEGER) "
        "FROM stats WHERE account_id = ?",
    /* Rooms (Step 16) */
    [LOBBY_STMT_INSERT_ROOM] =
        "INSERT INTO room_codes (code, server_addr, server_port, expires_at) "
        "VALUES (?, ?, ?, strftime('%s','now') + 600)",
    [LOBBY_STMT_FIND_ROOM] =
        "SELECT server_addr, server_port, status FROM room_codes WHERE code = ?",
    [LOBBY_STMT_UPDATE_ROOM_STATUS] =
        "UPDATE room_codes SET status = ? WHERE code = ?",
    [LOBBY_STMT_CLEANUP_ROOMS] =
        "DELETE FROM room_codes WHERE expires_at < strftime('%s','now') "
        "AND status IN ('active','expired')",
    /* Server Registry (Step 16) */
    [LOBBY_STMT_UPSERT_SERVER] =
        "INSERT OR REPLACE INTO game_servers "
        "(id, addr, port, max_rooms, current_rooms) VALUES (?, ?, ?, ?, ?)",
    [LOBBY_STMT_DELETE_SERVER] =
        "DELETE FROM game_servers WHERE id = ?",
    [LOBBY_STMT_UPDATE_HEARTBEAT] =
        "UPDATE game_servers SET current_rooms = ?, "
        "last_heartbeat = strftime('%s','now') WHERE id = ?",
    [LOBBY_STMT_PICK_SERVER] =
        "SELECT addr, port FROM game_servers "
        "WHERE current_rooms < max_rooms ORDER BY current_rooms LIMIT 1",
};

static int lobbydb_prepare_all(LobbyDB *ldb)
{
    for (int i = 0; i < (int)LOBBY_STMT__COUNT; i++) {
        if (!LOBBY_STMT_SQL[i]) continue;
        int rc = sqlite3_prepare_v2(ldb->db, LOBBY_STMT_SQL[i], -1,
                                    &ldb->stmts[i], NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "[db] Failed to prepare stmt %d: %s\n",
                    i, sqlite3_errmsg(ldb->db));
            return -1;
        }
    }
    return 0;
}

static void lobbydb_finalize_all(LobbyDB *ldb)
{
    for (int i = 0; i < (int)LOBBY_STMT__COUNT; i++) {
        if (ldb->stmts[i]) {
            sqlite3_finalize(ldb->stmts[i]);
            ldb->stmts[i] = NULL;
        }
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

bool lobbydb_open(LobbyDB *ldb, const char *path)
{
    memset(ldb, 0, sizeof(*ldb));

    int rc = sqlite3_open(path, &ldb->db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] Failed to open '%s': %s\n",
                path, ldb->db ? sqlite3_errmsg(ldb->db) : "out of memory");
        if (ldb->db) sqlite3_close(ldb->db);
        ldb->db = NULL;
        return false;
    }

    /* Set PRAGMAs for performance and safety */
    if (lobbydb_exec(ldb->db, "PRAGMA journal_mode = WAL;", "WAL mode") < 0)
        goto fail;
    if (lobbydb_exec(ldb->db, "PRAGMA foreign_keys = ON;", "foreign keys") < 0)
        goto fail;
    if (lobbydb_exec(ldb->db, "PRAGMA synchronous = NORMAL;", "synchronous") < 0)
        goto fail;
    if (lobbydb_exec(ldb->db, "PRAGMA busy_timeout = 5000;", "busy timeout") < 0)
        goto fail;
    if (lobbydb_exec(ldb->db, "PRAGMA mmap_size = 67108864;", "mmap") < 0)
        goto fail;

    /* Run migrations */
    if (lobbydb_migrate(ldb->db) < 0) goto fail;

    /* Prepare cached statements */
    if (lobbydb_prepare_all(ldb) < 0) goto fail;

    ldb->open = true;
    printf("[db] Opened '%s' (version %d)\n", path, LOBBY_SCHEMA_VERSION);
    return true;

fail:
    sqlite3_close(ldb->db);
    ldb->db = NULL;
    return false;
}

void lobbydb_close(LobbyDB *ldb)
{
    if (!ldb || !ldb->db) return;
    lobbydb_finalize_all(ldb);
    sqlite3_close(ldb->db);
    ldb->db = NULL;
    ldb->open = false;
    printf("[db] Closed\n");
}

sqlite3_stmt *lobbydb_stmt(LobbyDB *ldb, LobbyStmtID id)
{
    if ((int)id >= (int)LOBBY_STMT__COUNT) return NULL;
    if (!ldb->stmts[id]) return NULL;
    sqlite3_reset(ldb->stmts[id]);
    sqlite3_clear_bindings(ldb->stmts[id]);
    return ldb->stmts[id];
}

sqlite3 *lobbydb_handle(LobbyDB *ldb)
{
    return ldb ? ldb->db : NULL;
}
