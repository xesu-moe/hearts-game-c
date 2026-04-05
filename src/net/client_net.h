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
 *                disconnect/update/state/seat/has_new_state/peek_state/
 *                consume_state/ping_ms/reject_reason/send_add_ai/
 *                send_remove_ai/send_start_game(int ai_difficulty)/
 *                send_cmd/is_reconnecting/reconnect_attempt/
 *                reconnect_time_remaining/has_room_status/
 *                consume_room_status/set_username/get_reconnect_info()
 * @deps-requires: net/protocol.h (NetPlayerView, NetRejectReason,
 *                 PROTOCOL_VERSION, NET_ROOM_CODE_LEN, NET_AUTH_TOKEN_LEN),
 *                 net/reconnect.h (ReconnectState, reconnect_*),
 *                 core/input_cmd.h (InputCmd)
 * @deps-used-by: main.c, state_recv.c
 * @deps-last-changed: 2026-04-02 — Changed client_net_send_start_game() signature to accept int ai_difficulty
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
    CLIENT_NET_RECONNECTING,    /* auto-reconnect with backoff */
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

/* Set the auth token to use in the game server handshake.
 * Call before client_net_connect(). */
void client_net_set_auth_token(const uint8_t token[NET_AUTH_TOKEN_LEN]);

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

/* Peek at the next queued state without consuming it. Returns NULL if empty. */
const NetPlayerView *client_net_peek_state(void);

/* Copy the latest NetPlayerView into out and clear the flag. */
void client_net_consume_state(NetPlayerView *out);

/* Latest round-trip time in milliseconds, or -1 if unknown. */
int32_t client_net_ping_ms(void);

/* Rejection reason if state is CLIENT_NET_ERROR (NetRejectReason). */
uint8_t client_net_reject_reason(void);

/* True if the client is actively trying to reconnect. */
bool client_net_is_reconnecting(void);

/* Current reconnect attempt number (for UI). 0 if not reconnecting. */
int client_net_reconnect_attempt(void);

/* Seconds until next reconnect attempt. 0 if not reconnecting. */
float client_net_reconnect_time_remaining(void);

/* True if a room status update has been received since last consume. */
bool client_net_has_room_status(void);

/* Copy the latest room status and clear the flag. */
void client_net_consume_room_status(NetMsgRoomStatus *out);

/* Set the username to include in the handshake. Call before connect. */
void client_net_set_username(const char *name);

/* Copy current connection info for reconnect persistence.
 * Only valid when state is CLIENT_NET_CONNECTED. */
void client_net_get_reconnect_info(char *ip_out, uint16_t *port_out,
                                   char *room_code_out,
                                   uint8_t *session_token_out);

/* True if a server error message has been received since last consume. */
bool client_net_has_error(void);

/* Copy the error message into out (up to len bytes) and clear the flag.
 * Returns false if no error was pending. */
bool client_net_consume_error(char *out, size_t len);

/* True if a server chat/system message has been received since last consume. */
bool client_net_has_chat(void);

/* Copy the next chat message into out (up to len bytes) and dequeue.
 * color_out receives (r,g,b) — pass NULL to ignore.
 * transmute_id_out receives tooltip id (-1=none) — pass NULL to ignore.
 * highlight_out receives underline substring (32 bytes) — pass NULL to ignore.
 * Returns false if no chat was pending. */
bool client_net_consume_chat(char *out, size_t len,
                             uint8_t color_out[3],
                             int16_t *transmute_id_out,
                             char highlight_out[32]);

/* ================================================================
 * Pass Confirmation Queue (async toss animation)
 * ================================================================ */

/* Consume the next pending pass confirmation. Returns seat (0-3) or -1 if none. */
int client_net_consume_pass_confirmed(void);

/* Reset all pass confirmation tracking (call on new round). */
void client_net_reset_pass_confirmed(void);

/* ================================================================
 * Command Sending (used by Step 8)
 * ================================================================ */

/* Send a request to add an AI player to the waiting room.
 * Returns 0 on success, -1 on error (not connected). */
int client_net_send_add_ai(void);

/* Send a request to remove the last AI player from the waiting room.
 * Returns 0 on success, -1 on error (not connected). */
int client_net_send_remove_ai(void);

/* Send a request to start the game (room creator only).
 * ai_difficulty: 0=casual, 1=competitive.
 * Returns 0 on success, -1 on error (not connected). */
int client_net_send_start_game(int ai_difficulty);

/* Serialize and send an InputCmd to the server. Client-only commands
 * (hover, drag, settings) are silently filtered out.
 * Returns 0 on success, -1 on error (not connected, buffer full). */
int client_net_send_cmd(const InputCmd *cmd);

#endif /* CLIENT_NET_H */
