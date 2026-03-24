#ifndef CLIENT_NET_H
#define CLIENT_NET_H

/* ============================================================
 * Client Connection Manager
 *
 * Manages a single TCP connection to a game server. Handles
 * connect, handshake, per-frame send/recv, ping, and state
 * storage. Steps 8-9 consume the stored NetPlayerView.
 *
 * @deps-exports: ClientNetState, client_net_init/shutdown/connect/
 *                disconnect/update/state/seat/has_new_state/
 *                consume_state/ping_ms/reject_reason/send_cmd
 * @deps-requires: net/protocol.h (NetPlayerView, NetRejectReason,
 *                 PROTOCOL_VERSION, NET_ROOM_CODE_LEN, NET_AUTH_TOKEN_LEN),
 *                 core/input_cmd.h (InputCmd)
 * @deps-used-by: main.c, (future) cmd_send.c, state_recv.c
 * @deps-last-changed: 2026-03-23 — Step 7: Initial creation
 * ============================================================ */

#include <stdbool.h>
#include <stdint.h>

#include "net/protocol.h"
#include "core/input_cmd.h"

/* ================================================================
 * Client Connection State
 * ================================================================ */

typedef enum ClientNetState {
    CLIENT_NET_DISCONNECTED = 0,
    CLIENT_NET_CONNECTING,      /* TCP connect in progress */
    CLIENT_NET_HANDSHAKING,     /* TCP up, handshake sent, waiting ACK */
    CLIENT_NET_CONNECTED,       /* Handshake complete, in-game */
    CLIENT_NET_ERROR            /* Connection failed or rejected */
} ClientNetState;

/* ================================================================
 * Lifecycle
 * ================================================================ */

/* Initialize the client networking subsystem. Call once at startup. */
void client_net_init(void);

/* Shut down and free all networking resources. Call at exit. */
void client_net_shutdown(void);

/* ================================================================
 * Connection
 * ================================================================ */

/* Begin connecting to a game server. ip must be a dotted-quad address
 * (no DNS). room_code may be empty string to create a new room. */
void client_net_connect(const char *ip, uint16_t port, const char *room_code);

/* Gracefully disconnect from the server. */
void client_net_disconnect(void);

/* ================================================================
 * Per-Frame Update
 * ================================================================ */

/* Poll sockets, process incoming messages, send pings.
 * Call once per frame in the main loop, before game_update.
 * dt is the raw frame delta time in seconds. */
void client_net_update(float dt);

/* ================================================================
 * Query API
 * ================================================================ */

/* Current high-level connection state. */
ClientNetState client_net_state(void);

/* Assigned seat (0-3), or -1 if not yet assigned. */
int client_net_seat(void);

/* True if a new NetPlayerView has been received since last consume. */
bool client_net_has_new_state(void);

/* Copy the latest NetPlayerView into out and clear the flag. */
void client_net_consume_state(NetPlayerView *out);

/* Latest round-trip time in milliseconds, or -1 if unknown. */
int32_t client_net_ping_ms(void);

/* Rejection reason if state is CLIENT_NET_ERROR (NetRejectReason). */
uint8_t client_net_reject_reason(void);

/* ================================================================
 * Command Sending (used by Step 8)
 * ================================================================ */

/* Serialize and send an InputCmd to the server. Client-only commands
 * (hover, drag, settings) are silently filtered out.
 * Returns 0 on success, -1 on error (not connected, buffer full). */
int client_net_send_cmd(const InputCmd *cmd);

#endif /* CLIENT_NET_H */
