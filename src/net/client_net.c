/* ============================================================
 * @deps-implements: net/client_net.h
 * @deps-requires: net/client_net.h (ClientNetState, client_net_*),
 *                 net/socket.h (NetSocket, net_socket_*),
 *                 net/protocol.h (NetMsg, NetMsgType, NetPlayerView,
 *                 NetInputCmd, NET_ADDR_LEN, PROTOCOL_VERSION,
 *                 NET_ROOM_CODE_LEN, NET_AUTH_TOKEN_LEN,
 *                 net_input_cmd_is_relevant, net_input_cmd_from_local),
 *                 net/reconnect.h (ReconnectState, reconnect_init,
 *                 reconnect_begin, reconnect_update, reconnect_cancel,
 *                 reconnect_is_active, RECONNECT_MAX_ATTEMPTS),
 *                 core/input_cmd.h (InputCmd),
 *                 string.h, stdio.h, time.h
 * @deps-last-changed: 2026-03-24 — Step 11: Exponential backoff reconnect logic
 * ============================================================ */

#define _POSIX_C_SOURCE 199309L /* clock_gettime */

#include "client_net.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "net/socket.h"
#include "net/protocol.h"
#include "net/reconnect.h"
#include "core/input_cmd.h"

/* ================================================================
 * Constants
 * ================================================================ */

#define PING_INTERVAL 2.0f /* seconds between pings */

/* ================================================================
 * File-scope state
 * ================================================================ */

static NetSocket      g_net;
static int            g_conn_id;       /* connection index, -1 if none */
static ClientNetState g_state;
static int8_t         g_seat;          /* assigned seat 0-3, or -1 */
static uint8_t        g_reject_reason; /* NetRejectReason if ERROR */
static char           g_room_code[NET_ROOM_CODE_LEN];

/* State update storage */
static NetPlayerView  g_latest_view;
static bool           g_has_new_state;

/* Ping tracking */
static uint32_t       g_ping_sequence;
static float          g_ping_timer;
static int32_t        g_ping_rtt_ms;

/* Reconnect (Step 11) */
static ReconnectState g_reconnect;
static char           g_last_ip[NET_ADDR_LEN];
static uint16_t       g_last_port;
static uint8_t        g_session_token[NET_AUTH_TOKEN_LEN];

/* Error display (Step 13) */
static char           g_error_msg[NET_MAX_CHAT_LEN];
static bool           g_has_error;

/* ================================================================
 * Internal helpers
 * ================================================================ */

static uint32_t get_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void reset_state(void)
{
    g_conn_id       = -1;
    g_state         = CLIENT_NET_DISCONNECTED;
    g_seat          = -1;
    g_reject_reason = 0;
    g_has_new_state = false;
    g_ping_sequence = 0;
    g_ping_timer    = 0.0f;
    g_ping_rtt_ms   = -1;
    memset(g_room_code, 0, sizeof(g_room_code));
    memset(g_session_token, 0, sizeof(g_session_token));
    reconnect_init(&g_reconnect);
    g_error_msg[0] = '\0';
    g_has_error = false;
}

static void send_handshake(void)
{
    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_HANDSHAKE;
    msg.handshake.protocol_version = PROTOCOL_VERSION;
    memcpy(msg.handshake.room_code, g_room_code, NET_ROOM_CODE_LEN);

    /* If reconnecting, send session token so server can match our seat */
    if (reconnect_is_active(&g_reconnect))
        memcpy(msg.handshake.auth_token, g_session_token, NET_AUTH_TOKEN_LEN);

    if (net_socket_send_msg(&g_net, g_conn_id, &msg) < 0) {
        fprintf(stderr, "[client_net] Failed to send handshake\n");
        g_state = CLIENT_NET_ERROR;
        return;
    }

    g_state = CLIENT_NET_HANDSHAKING;
    printf("[client_net] Handshake sent\n");
}

static void handle_message(const NetMsg *msg)
{
    switch (msg->type) {
    case NET_MSG_HANDSHAKE_ACK:
        if (g_state != CLIENT_NET_HANDSHAKING) {
            printf("[client_net] Unexpected HANDSHAKE_ACK in state %d\n",
                   g_state);
            break;
        }
        g_seat  = (int8_t)msg->handshake_ack.assigned_seat;
        g_state = CLIENT_NET_CONNECTED;
        g_ping_timer = 0.0f;
        g_ping_rtt_ms = -1;
        memcpy(g_session_token, msg->handshake_ack.session_token,
               NET_AUTH_TOKEN_LEN);
        if (reconnect_is_active(&g_reconnect)) {
            printf("[client_net] Reconnected successfully, seat %d\n", g_seat);
            reconnect_cancel(&g_reconnect);
        } else {
            printf("[client_net] Connected, seat %d\n", g_seat);
        }
        break;

    case NET_MSG_HANDSHAKE_REJECT:
        g_reject_reason = msg->handshake_reject.reason;
        printf("[client_net] Rejected (reason %d)\n", g_reject_reason);
        net_socket_close(&g_net, g_conn_id);
        g_conn_id = -1;
        if (reconnect_is_active(&g_reconnect)) {
            /* Stay in RECONNECTING — will retry via backoff */
            g_state = CLIENT_NET_RECONNECTING;
        } else {
            g_state = CLIENT_NET_ERROR;
        }
        break;

    case NET_MSG_STATE_UPDATE:
        memcpy(&g_latest_view, &msg->state_update, sizeof(NetPlayerView));
        g_has_new_state = true;
        break;

    case NET_MSG_PONG: {
        uint32_t now = get_monotonic_ms();
        g_ping_rtt_ms = (int32_t)(now - msg->pong.echo_timestamp_ms);
        if (g_ping_rtt_ms < 0) g_ping_rtt_ms = 0;
        break;
    }

    case NET_MSG_ERROR:
        fprintf(stderr, "[client_net] Server error: %s\n",
                msg->error.message);
        strncpy(g_error_msg, msg->error.message, NET_MAX_CHAT_LEN - 1);
        g_error_msg[NET_MAX_CHAT_LEN - 1] = '\0';
        g_has_error = true;
        break;

    case NET_MSG_DISCONNECT:
        printf("[client_net] Server disconnected\n");
        net_socket_close(&g_net, g_conn_id);
        g_conn_id = -1;
        g_state = CLIENT_NET_DISCONNECTED;
        break;

    default:
        printf("[client_net] Unexpected message type %d\n", msg->type);
        break;
    }
}

/* ================================================================
 * Public API — Lifecycle
 * ================================================================ */

void client_net_init(void)
{
    if (net_socket_init(&g_net, 1) < 0) {
        fprintf(stderr, "[client_net] Failed to init socket\n");
        g_state = CLIENT_NET_ERROR;
        return;
    }
    reset_state();
    printf("[client_net] Initialized\n");
}

void client_net_shutdown(void)
{
    if (g_conn_id >= 0) {
        /* Send graceful disconnect if connected */
        if (g_state == CLIENT_NET_CONNECTED ||
            g_state == CLIENT_NET_HANDSHAKING) {
            NetMsg msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = NET_MSG_DISCONNECT;
            msg.disconnect.reason = NET_DISCONNECT_NORMAL;
            net_socket_send_msg(&g_net, g_conn_id, &msg);
        }
        net_socket_close(&g_net, g_conn_id);
    }
    net_socket_shutdown(&g_net);
    reset_state();
    printf("[client_net] Shutdown\n");
}

/* ================================================================
 * Public API — Connection
 * ================================================================ */

void client_net_connect(const char *ip, uint16_t port, const char *room_code)
{
    /* Disconnect if already active */
    if (g_state != CLIENT_NET_DISCONNECTED && g_state != CLIENT_NET_ERROR)
        client_net_disconnect();

    reset_state();

    /* Store connection params for potential reconnect */
    if (ip)
        strncpy(g_last_ip, ip, NET_ADDR_LEN - 1);
    g_last_port = port;

    /* Store room code */
    if (room_code)
        strncpy(g_room_code, room_code, NET_ROOM_CODE_LEN - 1);

    g_conn_id = net_socket_connect(&g_net, ip, port);
    if (g_conn_id < 0) {
        fprintf(stderr, "[client_net] Failed to initiate connection to %s:%d\n",
                ip, port);
        g_state = CLIENT_NET_ERROR;
        return;
    }

    g_state = CLIENT_NET_CONNECTING;
    printf("[client_net] Connecting to %s:%d\n", ip, port);
}

void client_net_disconnect(void)
{
    reconnect_cancel(&g_reconnect);

    if (g_conn_id < 0 || g_state == CLIENT_NET_DISCONNECTED)
        return;

    /* Send graceful disconnect if connected */
    if (g_state == CLIENT_NET_CONNECTED ||
        g_state == CLIENT_NET_HANDSHAKING) {
        NetMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = NET_MSG_DISCONNECT;
        msg.disconnect.reason = NET_DISCONNECT_NORMAL;
        net_socket_send_msg(&g_net, g_conn_id, &msg);
    }

    net_socket_close(&g_net, g_conn_id);
    g_conn_id = -1;
    g_state = CLIENT_NET_DISCONNECTED;
    g_seat = -1;
    printf("[client_net] Disconnected\n");
}

/* ================================================================
 * Public API — Per-Frame Update
 * ================================================================ */

void client_net_update(float dt)
{
    if (g_state == CLIENT_NET_DISCONNECTED || g_state == CLIENT_NET_ERROR)
        return;

    /* --- RECONNECTING state machine --- */
    if (g_state == CLIENT_NET_RECONNECTING) {
        /* Poll sockets if we have an active TCP attempt */
        if (g_conn_id >= 0)
            net_socket_update(&g_net);

        /* Check if current reconnect TCP attempt failed */
        if (g_conn_id >= 0 &&
            net_socket_state(&g_net, g_conn_id) == NET_CONN_DISCONNECTED) {
            g_conn_id = -1;
        }

        /* If no active TCP, tick backoff and maybe start new attempt */
        if (g_conn_id < 0) {
            if (reconnect_exhausted(&g_reconnect)) {
                printf("[client_net] Reconnect failed after %d attempts\n",
                       RECONNECT_MAX_ATTEMPTS);
                g_state = CLIENT_NET_DISCONNECTED;
                reconnect_cancel(&g_reconnect);
                return;
            }
            if (reconnect_update(&g_reconnect, dt)) {
                printf("[client_net] Reconnect attempt %d/%d\n",
                       reconnect_attempt(&g_reconnect),
                       RECONNECT_MAX_ATTEMPTS);
                g_conn_id = net_socket_connect(&g_net, g_last_ip,
                                                g_last_port);
                /* if connect fails, g_conn_id stays -1, will retry */
            }
            return;
        }

        /* Active TCP in progress — check if connected, send handshake.
         * Note: send_handshake() changes g_state from RECONNECTING to
         * HANDSHAKING. The message loop below may then transition to
         * CONNECTED in the same tick if ACK arrives immediately. */
        if (net_socket_state(&g_net, g_conn_id) == NET_CONN_CONNECTED) {
            send_handshake();
        }

        /* Process messages (HANDSHAKE_ACK transitions to CONNECTED) */
        if (g_conn_id >= 0) {
            NetMsg msg;
            while (net_socket_recv_msg(&g_net, g_conn_id, &msg)) {
                handle_message(&msg);
                if (g_conn_id < 0) break;
            }
        }
        return;
    }

    /* --- Normal (non-reconnecting) flow --- */

    /* Poll sockets */
    net_socket_update(&g_net);

    /* Check for connection loss */
    if (g_conn_id >= 0 &&
        net_socket_state(&g_net, g_conn_id) == NET_CONN_DISCONNECTED) {
        if (g_state == CLIENT_NET_CONNECTED) {
            /* Unexpected loss — start auto-reconnect */
            printf("[client_net] Connection lost, starting reconnect\n");
            g_state = CLIENT_NET_RECONNECTING;
            g_conn_id = -1;
            g_seat = -1;
            g_has_new_state = false; /* stale data from before disconnect */
            reconnect_begin(&g_reconnect, g_last_ip, g_last_port,
                            g_room_code, g_session_token);
            return;
        } else {
            printf("[client_net] Connection failed\n");
            g_state = CLIENT_NET_ERROR;
        }
        g_conn_id = -1;
        g_seat = -1;
        return;
    }

    /* CONNECTING: wait for TCP to complete, then send handshake */
    if (g_state == CLIENT_NET_CONNECTING &&
        net_socket_state(&g_net, g_conn_id) == NET_CONN_CONNECTED) {
        send_handshake();
    }

    /* Process all incoming messages */
    if (g_conn_id >= 0) {
        NetMsg msg;
        while (net_socket_recv_msg(&g_net, g_conn_id, &msg)) {
            handle_message(&msg);
            /* Stop processing if connection was closed in handler */
            if (g_conn_id < 0) break;
        }
    }

    /* Ping timer (connected only) */
    if (g_state == CLIENT_NET_CONNECTED && g_conn_id >= 0) {
        g_ping_timer += dt;
        if (g_ping_timer >= PING_INTERVAL) {
            NetMsg ping;
            memset(&ping, 0, sizeof(ping));
            ping.type = NET_MSG_PING;
            ping.ping.sequence = g_ping_sequence++;
            ping.ping.timestamp_ms = get_monotonic_ms();
            net_socket_send_msg(&g_net, g_conn_id, &ping);
            g_ping_timer = 0.0f;
        }
    }
}

/* ================================================================
 * Public API — Query
 * ================================================================ */

ClientNetState client_net_state(void)
{
    return g_state;
}

int client_net_seat(void)
{
    return g_seat;
}

bool client_net_has_new_state(void)
{
    return g_has_new_state;
}

void client_net_consume_state(NetPlayerView *out)
{
    if (out)
        memcpy(out, &g_latest_view, sizeof(NetPlayerView));
    g_has_new_state = false;
}

int32_t client_net_ping_ms(void)
{
    return g_ping_rtt_ms;
}

uint8_t client_net_reject_reason(void)
{
    return g_reject_reason;
}

bool client_net_is_reconnecting(void)
{
    return g_state == CLIENT_NET_RECONNECTING;
}

int client_net_reconnect_attempt(void)
{
    return reconnect_attempt(&g_reconnect);
}

float client_net_reconnect_time_remaining(void)
{
    return reconnect_time_remaining(&g_reconnect);
}

bool client_net_has_error(void)
{
    return g_has_error;
}

bool client_net_consume_error(char *out, size_t len)
{
    if (!g_has_error) return false;
    if (out && len > 0) {
        strncpy(out, g_error_msg, len - 1);
        out[len - 1] = '\0';
    }
    g_has_error = false;
    g_error_msg[0] = '\0';
    return true;
}

/* ================================================================
 * Public API — Command Sending
 * ================================================================ */

int client_net_send_cmd(const InputCmd *cmd)
{
    if (g_state != CLIENT_NET_CONNECTED || g_conn_id < 0)
        return -1;

    if (!net_input_cmd_is_relevant((uint8_t)cmd->type))
        return 0; /* silently skip client-only commands */

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_INPUT_CMD;
    net_input_cmd_from_local(cmd, &msg.input_cmd);

    return net_socket_send_msg(&g_net, g_conn_id, &msg);
}
