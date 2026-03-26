/* ============================================================
 * @deps-implements: lobby/auth.h
 * @deps-requires: lobby/auth.h (AuthResult, AuthAccountInfo),
 *                 lobby/db.h (LobbyDB, LobbyStmtID, lobbydb_stmt),
 *                 vendor/tweetnacl.h (crypto_sign_open),
 *                 sqlite3.h, stdio.h, string.h, sys/random.h
 * @deps-last-changed: 2026-03-24 — Step 15: Account System
 * ============================================================ */

#include "auth.h"

#include <sqlite3.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>

#include "vendor/tweetnacl.h"

/* ================================================================
 * Helpers
 * ================================================================ */

static bool auth_random_bytes(uint8_t *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = getrandom(buf, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[auth] getrandom failed: %s\n", strerror(errno));
            return false;
        }
        buf += (size_t)n;
        len -= (size_t)n;
    }
    return true;
}

/* Validate username: non-empty, null-terminated within max length,
 * alphanumeric + underscore only, 3-31 chars. */
static bool auth_validate_username(const char *username, size_t max_len)
{
    size_t len = 0;
    for (size_t i = 0; i < max_len; i++) {
        if (username[i] == '\0') {
            len = i;
            break;
        }
        char c = username[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
        len = i + 1;
    }
    return len >= 3 && len <= 31;
}

/* ================================================================
 * Registration
 * ================================================================ */

AuthResult auth_register(LobbyDB *ldb,
                         const char *username,
                         const uint8_t public_key[AUTH_PK_LEN])
{
    if (!auth_validate_username(username, 32)) {
        return AUTH_ERR_INVALID_INPUT;
    }

    /* Check if public key is already registered */
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_FIND_BY_PUBKEY);
    if (!stmt) return AUTH_ERR_DB_ERROR;
    sqlite3_bind_blob(stmt, 1, public_key, AUTH_PK_LEN, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        return AUTH_ERR_PUBKEY_TAKEN;
    }

    /* Insert account */
    stmt = lobbydb_stmt(ldb, LOBBY_STMT_REGISTER_ACCOUNT);
    if (!stmt) return AUTH_ERR_DB_ERROR;
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, public_key, AUTH_PK_LEN, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_CONSTRAINT) {
        return AUTH_ERR_USERNAME_TAKEN;
    }
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[auth] Register failed: %s\n",
                sqlite3_errmsg(lobbydb_handle(ldb)));
        return AUTH_ERR_DB_ERROR;
    }

    /* Get the new account ID */
    int32_t account_id = (int32_t)sqlite3_last_insert_rowid(lobbydb_handle(ldb));

    /* Initialize stats row */
    stmt = lobbydb_stmt(ldb, LOBBY_STMT_INIT_STATS);
    if (stmt) {
        sqlite3_bind_int(stmt, 1, account_id);
        sqlite3_step(stmt);
    }

    printf("[auth] Registered user '%s' (id=%d)\n", username, account_id);
    return AUTH_OK;
}

/* ================================================================
 * Account Lookup
 * ================================================================ */

AuthResult auth_find_account(LobbyDB *ldb,
                             const char *username,
                             int32_t *account_id_out,
                             uint8_t pk_out[AUTH_PK_LEN])
{
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_FIND_BY_USERNAME);
    if (!stmt) return AUTH_ERR_DB_ERROR;
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        return AUTH_ERR_UNKNOWN_USER;
    }

    *account_id_out = sqlite3_column_int(stmt, 0);
    const void *pk_blob = sqlite3_column_blob(stmt, 1);
    int pk_len = sqlite3_column_bytes(stmt, 1);
    if (pk_blob && pk_len == AUTH_PK_LEN) {
        memcpy(pk_out, pk_blob, AUTH_PK_LEN);
    } else {
        fprintf(stderr, "[auth] Corrupt public_key for user '%s'\n", username);
        return AUTH_ERR_DB_ERROR;
    }

    return AUTH_OK;
}

/* ================================================================
 * Challenge Generation
 * ================================================================ */

bool auth_generate_challenge(uint8_t nonce_out[AUTH_CHALLENGE_LEN])
{
    return auth_random_bytes(nonce_out, AUTH_CHALLENGE_LEN);
}

/* ================================================================
 * Signature Verification + Login
 * ================================================================ */

AuthResult auth_verify_and_login(LobbyDB *ldb,
                                 int32_t account_id,
                                 const uint8_t nonce[AUTH_CHALLENGE_LEN],
                                 const uint8_t signature[AUTH_SIG_LEN],
                                 const uint8_t stored_pk[AUTH_PK_LEN],
                                 uint8_t token_out[AUTH_TOKEN_LEN],
                                 AuthAccountInfo *info_out)
{
    /* Reconstruct the signed message: sig(64) || nonce(32) */
    uint8_t sm[AUTH_SIG_LEN + AUTH_CHALLENGE_LEN];
    memcpy(sm, signature, AUTH_SIG_LEN);
    memcpy(sm + AUTH_SIG_LEN, nonce, AUTH_CHALLENGE_LEN);

    /* Verify signature — m must be >= sizeof(sm) for crypto_sign_open */
    uint8_t m[AUTH_SIG_LEN + AUTH_CHALLENGE_LEN];
    unsigned long long mlen = 0;
    if (crypto_sign_open(m, &mlen, sm, sizeof(sm), stored_pk) != 0) {
        printf("[auth] Invalid signature for account %d\n", account_id);
        return AUTH_ERR_INVALID_SIG;
    }

    /* Generate session token */
    if (!auth_random_bytes(token_out, AUTH_TOKEN_LEN))
        return AUTH_ERR_DB_ERROR;

    /* Insert session */
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_CREATE_SESSION);
    if (!stmt) return AUTH_ERR_DB_ERROR;
    sqlite3_bind_blob(stmt, 1, token_out, AUTH_TOKEN_LEN, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, account_id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "[auth] Session insert failed: %s\n",
                sqlite3_errmsg(lobbydb_handle(ldb)));
        return AUTH_ERR_DB_ERROR;
    }

    /* Update last_login */
    stmt = lobbydb_stmt(ldb, LOBBY_STMT_UPDATE_LAST_LOGIN);
    if (stmt) {
        sqlite3_bind_int(stmt, 1, account_id);
        sqlite3_step(stmt);
    }

    /* Fetch stats */
    memset(info_out, 0, sizeof(*info_out));
    info_out->account_id = account_id;
    stmt = lobbydb_stmt(ldb, LOBBY_STMT_GET_STATS);
    if (stmt) {
        sqlite3_bind_int(stmt, 1, account_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            info_out->games_played = (uint32_t)sqlite3_column_int(stmt, 0);
            info_out->games_won = (uint32_t)sqlite3_column_int(stmt, 1);
            info_out->elo_rating = (int32_t)sqlite3_column_int(stmt, 2);
        }
    }

    printf("[auth] Login successful for account %d\n", account_id);
    return AUTH_OK;
}

/* ================================================================
 * Session Management
 * ================================================================ */

int32_t auth_validate_token(LobbyDB *ldb,
                            const uint8_t token[AUTH_TOKEN_LEN])
{
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_FIND_SESSION);
    if (!stmt) return -1;
    sqlite3_bind_blob(stmt, 1, token, AUTH_TOKEN_LEN, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        return -1;
    }
    return sqlite3_column_int(stmt, 0);
}

void auth_logout(LobbyDB *ldb, const uint8_t token[AUTH_TOKEN_LEN])
{
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_DELETE_SESSION);
    if (!stmt) return;
    sqlite3_bind_blob(stmt, 1, token, AUTH_TOKEN_LEN, SQLITE_STATIC);
    sqlite3_step(stmt);
}

AuthResult auth_change_username(LobbyDB *ldb, int32_t account_id,
                                const char *new_username)
{
    if (!auth_validate_username(new_username, 32)) {
        return AUTH_ERR_INVALID_INPUT;
    }

    /* Check if the new username is taken by someone else */
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_FIND_BY_USERNAME);
    if (!stmt) return AUTH_ERR_DB_ERROR;
    sqlite3_bind_text(stmt, 1, new_username, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int32_t existing_id = sqlite3_column_int(stmt, 0);
        if (existing_id != account_id) {
            return AUTH_ERR_USERNAME_TAKEN;
        }
        /* Same user, same name — no-op */
        return AUTH_OK;
    }

    /* Update username */
    stmt = lobbydb_stmt(ldb, LOBBY_STMT_CHANGE_USERNAME);
    if (!stmt) return AUTH_ERR_DB_ERROR;
    sqlite3_bind_text(stmt, 1, new_username, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, account_id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "[auth] Failed to change username: %s\n",
                sqlite3_errmsg(lobbydb_handle(ldb)));
        return AUTH_ERR_DB_ERROR;
    }

    printf("[auth] Account %d changed username to '%s'\n",
           account_id, new_username);
    return AUTH_OK;
}

void auth_cleanup_expired(LobbyDB *ldb)
{
    sqlite3_stmt *stmt = lobbydb_stmt(ldb, LOBBY_STMT_DELETE_EXPIRED);
    if (!stmt) return;
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        int changes = sqlite3_changes(lobbydb_handle(ldb));
        if (changes > 0) {
            printf("[auth] Cleaned up %d expired sessions\n", changes);
        }
    }
}
