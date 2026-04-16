/* ============================================================
 * Client Identity — Ed25519 Keypair Management
 *
 * Generates, loads, and stores the player's Ed25519 keypair
 * at ~/.config/hollow-hearts/identity.key (64 bytes raw binary).
 * Signs challenge nonces for lobby authentication.
 *
 * @deps-exports: Identity, identity_load_or_create, identity_sign,
 *                identity_load_username, identity_save_username,
 *                identity_export, identity_import, identity_backup_exists
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

/* Load keypair from ~/.config/hollow-hearts/identity.key.
 * If the file doesn't exist, generates a new keypair and saves it.
 * Returns true on success. */
bool identity_load_or_create(Identity *id);

/* Sign a message with the secret key.
 * Writes a 64-byte detached Ed25519 signature to sig_out.
 * Returns true on success. */
bool identity_sign(const Identity *id,
                   const uint8_t *msg, size_t msg_len,
                   uint8_t *sig_out);

/* Load stored username from ~/.config/hollow-hearts/username.txt.
 * Returns true if a username was found and copied to buf. */
bool identity_load_username(char *buf, size_t buflen);

/* Save username to ~/.config/hollow-hearts/username.txt.
 * Returns true on success. */
bool identity_save_username(const char *username);

/* Export identity.key to ~/hollow-hearts-identity.bak.
 * Returns true on success. */
bool identity_export(void);

/* Import identity from ~/hollow-hearts-identity.bak, overwriting current key.
 * Reloads the Identity struct with the imported key.
 * Returns true on success. */
bool identity_import(Identity *id);

/* Returns true if ~/hollow-hearts-identity.bak exists and is valid. */
bool identity_backup_exists(void);

#endif /* IDENTITY_H */
