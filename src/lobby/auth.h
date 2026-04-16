/* ============================================================
 * Lobby Server — Authentication Logic
 *
 * Ed25519 challenge-response auth: register with public key,
 * login by signing a nonce, session token management.
 *
 * @deps-exports: AuthResult, AuthAccountInfo (elo_rating: int32_t)
 * @deps-requires: lobby/db.h (LobbyDB)
 * @deps-used-by: lobby/lobby_net.c
 * @deps-last-changed: 2026-03-26 — Step 22.5: AuthAccountInfo.elo_rating uint16_t→int32_t
 * ============================================================ */

#ifndef LOBBY_AUTH_H
#define LOBBY_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "db.h"

#define AUTH_PK_LEN        32
#define AUTH_SIG_LEN       64
#define AUTH_CHALLENGE_LEN 32
#define AUTH_TOKEN_LEN     32
#define AUTH_SESSION_TTL   86400 /* 24 hours in seconds */

typedef enum AuthResult {
    AUTH_OK = 0,
    AUTH_ERR_USERNAME_TAKEN,
    AUTH_ERR_PUBKEY_TAKEN,
    AUTH_ERR_UNKNOWN_USER,
    AUTH_ERR_INVALID_SIG,
    AUTH_ERR_DB_ERROR,
    AUTH_ERR_INVALID_INPUT
} AuthResult;

typedef struct AuthAccountInfo {
    int32_t  account_id;
    int32_t  elo_rating;
    uint32_t games_played;
    uint32_t games_won;
} AuthAccountInfo;

/* Register a new account.
 * Inserts username + public_key into accounts table, creates stats row.
 * Returns AUTH_OK or an error code. */
AuthResult auth_register(LobbyDB *ldb,
                         const char *username,
                         const uint8_t public_key[AUTH_PK_LEN]);

/* Look up account by username.
 * On success: sets *account_id_out and copies public_key to pk_out.
 * Returns AUTH_OK or AUTH_ERR_UNKNOWN_USER. */
AuthResult auth_find_account(LobbyDB *ldb,
                             const char *username,
                             int32_t *account_id_out,
                             uint8_t pk_out[AUTH_PK_LEN]);

/* Look up account by public key.
 * On success: sets *account_id_out and copies username to username_out.
 * Returns AUTH_OK or AUTH_ERR_UNKNOWN_USER. */
AuthResult auth_find_account_by_key(LobbyDB *ldb,
                                    const uint8_t public_key[AUTH_PK_LEN],
                                    int32_t *account_id_out,
                                    char *username_out, size_t username_buflen);

/* Generate a random 32-byte challenge nonce. Returns false on RNG failure. */
bool auth_generate_challenge(uint8_t nonce_out[AUTH_CHALLENGE_LEN]);

/* Verify a signed challenge and create a session if valid.
 * On success: writes session token to token_out, fills info_out.
 * Returns AUTH_OK or error code. */
AuthResult auth_verify_and_login(LobbyDB *ldb,
                                 int32_t account_id,
                                 const uint8_t nonce[AUTH_CHALLENGE_LEN],
                                 const uint8_t signature[AUTH_SIG_LEN],
                                 const uint8_t stored_pk[AUTH_PK_LEN],
                                 uint8_t token_out[AUTH_TOKEN_LEN],
                                 AuthAccountInfo *info_out);

/* Validate a session token.
 * Returns account_id if valid, -1 if expired/invalid. */
int32_t auth_validate_token(LobbyDB *ldb,
                            const uint8_t token[AUTH_TOKEN_LEN]);

/* Change username for an account.
 * Validates new username format and checks uniqueness.
 * Returns AUTH_OK, AUTH_ERR_USERNAME_TAKEN, or AUTH_ERR_INVALID_INPUT. */
AuthResult auth_change_username(LobbyDB *ldb, int32_t account_id,
                                const char *new_username);

/* Delete a session (logout). */
void auth_logout(LobbyDB *ldb, const uint8_t token[AUTH_TOKEN_LEN]);

/* Purge expired sessions. Call periodically. */
void auth_cleanup_expired(LobbyDB *ldb);

#endif /* LOBBY_AUTH_H */
