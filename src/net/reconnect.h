/* ============================================================
 * Client-side reconnection with exponential backoff.
 *
 * @deps-exports: ReconnectState, reconnect_init, reconnect_begin,
 *                reconnect_update, reconnect_cancel, reconnect_is_active,
 *                reconnect_attempt, reconnect_time_remaining,
 *                reconnect_exhausted, RECONNECT_MAX_ATTEMPTS,
 *                RECONNECT_BASE_DELAY, RECONNECT_MAX_DELAY
 * @deps-requires: net/protocol.h (NET_ADDR_LEN, NET_ROOM_CODE_LEN,
 *                 NET_AUTH_TOKEN_LEN)
 * @deps-used-by: net/client_net.c
 * @deps-last-changed: 2026-03-24 — Step 11: Disconnect & Reconnect
 * ============================================================ */

#ifndef RECONNECT_H
#define RECONNECT_H

#include <stdbool.h>
#include <stdint.h>

#include "net/protocol.h"

/* ================================================================
 * Constants
 * ================================================================ */

#define RECONNECT_MAX_ATTEMPTS  10
#define RECONNECT_BASE_DELAY    1.0f  /* seconds */
#define RECONNECT_MAX_DELAY    15.0f  /* seconds */

/* ================================================================
 * State
 * ================================================================ */

typedef struct ReconnectState {
    bool     active;
    char     server_ip[NET_ADDR_LEN];
    uint16_t server_port;
    char     room_code[NET_ROOM_CODE_LEN];
    uint8_t  session_token[NET_AUTH_TOKEN_LEN];
    int      attempt;       /* current attempt (1-based after first try) */
    float    backoff_timer; /* seconds until next attempt */
} ReconnectState;

/* ================================================================
 * API
 * ================================================================ */

/* Initialize to inactive state. */
void reconnect_init(ReconnectState *rs);

/* Begin a reconnect sequence. First attempt is immediate (timer=0). */
void reconnect_begin(ReconnectState *rs, const char *ip, uint16_t port,
                     const char *room_code,
                     const uint8_t session_token[NET_AUTH_TOKEN_LEN]);

/* Tick the backoff timer. Returns true when it's time to attempt
 * a new connection. Returns false while waiting or if exhausted. */
bool reconnect_update(ReconnectState *rs, float dt);

/* Cancel the reconnect sequence. */
void reconnect_cancel(ReconnectState *rs);

/* Query: is a reconnect sequence active? */
bool reconnect_is_active(const ReconnectState *rs);

/* Query: current attempt number (for UI). */
int reconnect_attempt(const ReconnectState *rs);

/* Query: seconds until next attempt. */
float reconnect_time_remaining(const ReconnectState *rs);

/* Query: have all attempts been exhausted? */
bool reconnect_exhausted(const ReconnectState *rs);

#endif /* RECONNECT_H */
