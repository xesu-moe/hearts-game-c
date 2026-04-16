/* ============================================================
 * @deps-implements: net/reconnect.h
 * @deps-requires: net/reconnect.h (ReconnectState, RECONNECT_*),
 *                 net/protocol.h (NET_ADDR_LEN, NET_ROOM_CODE_LEN,
 *                 NET_AUTH_TOKEN_LEN)
 * @deps-last-changed: 2026-03-24 — Step 11: Disconnect & Reconnect
 * ============================================================ */

#include "reconnect.h"

#include <string.h>

void reconnect_init(ReconnectState *rs)
{
    memset(rs, 0, sizeof(*rs));
}

void reconnect_begin(ReconnectState *rs, const char *ip, uint16_t port,
                     const char *room_code,
                     const uint8_t session_token[NET_AUTH_TOKEN_LEN])
{
    memset(rs, 0, sizeof(*rs));
    rs->active = true;
    rs->attempt = 0;
    rs->backoff_timer = 0.0f; /* immediate first attempt */

    if (ip)
        strncpy(rs->server_ip, ip, NET_ADDR_LEN - 1);
    rs->server_port = port;
    if (room_code)
        strncpy(rs->room_code, room_code, NET_ROOM_CODE_LEN - 1);
    if (session_token)
        memcpy(rs->session_token, session_token, NET_AUTH_TOKEN_LEN);
}

bool reconnect_update(ReconnectState *rs, float dt)
{
    if (!rs->active) return false;
    if (rs->attempt >= RECONNECT_MAX_ATTEMPTS) return false;

    rs->backoff_timer -= dt;
    if (rs->backoff_timer > 0.0f) return false;

    /* Time to attempt */
    rs->attempt++;

    /* Compute delay for NEXT attempt: base * 2^(attempt-1), capped */
    float delay = RECONNECT_BASE_DELAY * (float)(1 << (rs->attempt - 1));
    if (delay > RECONNECT_MAX_DELAY)
        delay = RECONNECT_MAX_DELAY;
    rs->backoff_timer = delay;

    return true;
}

void reconnect_cancel(ReconnectState *rs)
{
    rs->active = false;
}

bool reconnect_is_active(const ReconnectState *rs)
{
    return rs->active;
}

int reconnect_attempt(const ReconnectState *rs)
{
    return rs->attempt;
}

float reconnect_time_remaining(const ReconnectState *rs)
{
    if (!rs->active) return 0.0f;
    return rs->backoff_timer > 0.0f ? rs->backoff_timer : 0.0f;
}

bool reconnect_exhausted(const ReconnectState *rs)
{
    return rs->attempt >= RECONNECT_MAX_ATTEMPTS;
}
