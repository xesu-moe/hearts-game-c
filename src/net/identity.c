/* ============================================================
 * @deps-implements: net/identity.h
 * @deps-requires: net/identity.h (Identity),
 *                 vendor/tweetnacl.h (crypto_sign_keypair,
 *                 crypto_sign),
 *                 stdio.h, string.h, sys/stat.h, sys/types.h,
 *                 errno.h, stdlib.h
 * @deps-last-changed: 2026-03-24 — Step 15: Account System
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L

#include "identity.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "vendor/tweetnacl.h"

/* ================================================================
 * Path helpers
 * ================================================================ */

#define IDENTITY_DIR  ".hollow-hearts"
#define IDENTITY_FILE "identity.key"

/* Build full path: ~/.hollow-hearts/identity.key
 * Returns true on success. */
static bool identity_build_path(char *buf, size_t buflen)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        fprintf(stderr, "[identity] HOME not set\n");
        return false;
    }
    int n = snprintf(buf, buflen, "%s/%s/%s", home, IDENTITY_DIR, IDENTITY_FILE);
    return n > 0 && (size_t)n < buflen;
}

static bool identity_build_dir(char *buf, size_t buflen)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    int n = snprintf(buf, buflen, "%s/%s", home, IDENTITY_DIR);
    return n > 0 && (size_t)n < buflen;
}

/* ================================================================
 * Load or create keypair
 * ================================================================ */

bool identity_load_or_create(Identity *id)
{
    memset(id, 0, sizeof(*id));

    char path[512];
    if (!identity_build_path(path, sizeof(path))) return false;

    /* Try to load existing key */
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t nread = fread(id->secret_key, 1, IDENTITY_SK_LEN, f);
        fclose(f);
        if (nread == IDENTITY_SK_LEN) {
            memcpy(id->public_key, id->secret_key + 32, IDENTITY_PK_LEN);
            id->loaded = true;
            printf("[identity] Loaded keypair from %s\n", path);
            return true;
        }
        fprintf(stderr, "[identity] Corrupt key file (got %zu bytes, expected %d)\n",
                nread, IDENTITY_SK_LEN);
    }

    /* Generate new keypair */
    printf("[identity] Generating new Ed25519 keypair...\n");
    if (crypto_sign_keypair(id->public_key, id->secret_key) != 0) {
        fprintf(stderr, "[identity] crypto_sign_keypair failed\n");
        return false;
    }

    /* Create directory */
    char dir[512];
    if (!identity_build_dir(dir, sizeof(dir))) return false;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "[identity] mkdir '%s' failed: %s\n", dir, strerror(errno));
        return false;
    }

    /* Write secret key with restrictive permissions from the start */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        fprintf(stderr, "[identity] Cannot create '%s': %s\n", path, strerror(errno));
        return false;
    }
    f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        fprintf(stderr, "[identity] fdopen '%s' failed: %s\n", path, strerror(errno));
        return false;
    }
    size_t nwritten = fwrite(id->secret_key, 1, IDENTITY_SK_LEN, f);
    fclose(f); /* also closes fd */
    if (nwritten != IDENTITY_SK_LEN) {
        fprintf(stderr, "[identity] Failed to write key file\n");
        return false;
    }

    id->loaded = true;
    printf("[identity] Saved new keypair to %s\n", path);
    return true;
}

/* ================================================================
 * Sign a message (detached signature)
 * ================================================================ */

bool identity_sign(const Identity *id,
                   const uint8_t *msg, size_t msg_len,
                   uint8_t *sig_out)
{
    if (!id->loaded) return false;

    /* crypto_sign produces [64B sig || msg].
     * We only need the first 64 bytes (detached signature). */
    uint8_t sm[64 + 256]; /* challenge is 32B max, plenty of room */
    if (msg_len > 256) {
        fprintf(stderr, "[identity] Message too large for sign buffer (%zu)\n", msg_len);
        return false;
    }
    unsigned long long smlen = 0;
    if (crypto_sign(sm, &smlen, msg, (unsigned long long)msg_len,
                    id->secret_key) != 0) {
        return false;
    }

    memcpy(sig_out, sm, 64);
    return true;
}
