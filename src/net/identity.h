/* ============================================================
 * Client Identity — Ed25519 Keypair Management
 *
 * Generates, loads, and stores the player's Ed25519 keypair
 * at ~/.hollow-hearts/identity.key (64 bytes raw binary).
 * Signs challenge nonces for lobby authentication.
 *
 * @deps-exports: Identity, identity_load_or_create, identity_sign
 * @deps-requires: vendor/tweetnacl.h (crypto_sign_*)
 * @deps-used-by: (client login flow — Steps 19-20)
 * @deps-last-changed: 2026-03-24 — Step 15: Account System
 * ============================================================ */

#ifndef IDENTITY_H
#define IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IDENTITY_PK_LEN  32
#define IDENTITY_SK_LEN  64 /* Ed25519 sk: 32B seed || 32B pk = 64B total */

typedef struct Identity {
    uint8_t secret_key[IDENTITY_SK_LEN];
    uint8_t public_key[IDENTITY_PK_LEN]; /* copied from sk[32..64] */
    bool    loaded;
} Identity;

/* Load keypair from ~/.hollow-hearts/identity.key.
 * If the file doesn't exist, generates a new keypair and saves it.
 * Returns true on success. */
bool identity_load_or_create(Identity *id);

/* Sign a message with the secret key.
 * Writes a 64-byte detached Ed25519 signature to sig_out.
 * Returns true on success. */
bool identity_sign(const Identity *id,
                   const uint8_t *msg, size_t msg_len,
                   uint8_t *sig_out);

#endif /* IDENTITY_H */
