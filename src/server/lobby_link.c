/* ============================================================
 * @deps-implements: server/lobby_link.h
 * @deps-requires: server/lobby_link.h,
 *                 server/room.h (room_create_with_code, room_active_count,
 *                 NET_MAX_PLAYERS, MAX_ROOMS),
 *                 net/socket.h (NetSocket, net_socket_*),
 *                 net/protocol.h (NetMsg, NetMsgType),
 *                 core/clock.h (FIXED_DT),
 *                 stdio.h, string.h, time.h
 * @deps-last-changed: 2026-03-24 — Step 16: Room Code System
 * ============================================================ */

#define _POSIX_C_SOURCE 199309L

#include "lobby_link.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "room.h"
#include "net/socket.h"
#include "net/protocol.h"

/* ================================================================
 * Configuration
 * ================================================================ */

#define HEARTBEAT_INTERVAL  15.0 /* seconds between heartbeats */
#define RECONNECT_INTERVAL   5.0 /* seconds between reconnect attempts */

/* ================================================================
 * File-scope state
 * ================================================================ */

static NetSocket g_lobby;
static int       g_conn_id;       /* connection index (0 for single connection) */
static bool      g_initialized;
static bool      g_registered;    /* true after sending SERVER_REGISTER */

/* Config from init */
static char     g_lobby_addr[NET_ADDR_LEN];
static uint16_t g_lobby_port;
static char     g_self_addr[NET_ADDR_LEN];
static uint16_t g_self_port;
static uint16_t g_max_rooms;

/* Timing */
static double g_last_heartbeat;
static double g_last_reconnect;

/* ================================================================
 * Time helper
 * ================================================================ */

static double ll_time_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ================================================================
 * Internal: Send registration message
 * ================================================================ */

static void ll_send_register(void)
{
    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_SERVER_REGISTER;
    strncpy(msg.server_register.addr, g_self_addr, NET_ADDR_LEN - 1);
    msg.server_register.port = g_self_port;
    msg.server_register.max_rooms = g_max_rooms;
    msg.server_register.current_rooms = (uint16_t)room_active_count();
    net_socket_send_msg(&g_lobby, g_conn_id, &msg);
    g_registered = true;
    printf("[lobby-link] Sent SERVER_REGISTER to lobby (%s:%d)\n",
           g_lobby_addr, g_lobby_port);
}

/* ================================================================
 * Internal: Send heartbeat
 * ================================================================ */

static void ll_send_heartbeat(void)
{
    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_SERVER_HEARTBEAT;
    msg.server_heartbeat.current_rooms = (uint16_t)room_active_count();
    /* Count connected players across all rooms */
    uint16_t players = 0;
    for (int r = 0; r < MAX_ROOMS; r++) {
        Room *room = room_get(r);
        if (room && room->status != ROOM_INACTIVE) {
            players += (uint16_t)room->connected_count;
        }
    }
    msg.server_heartbeat.current_players = players;
    net_socket_send_msg(&g_lobby, g_conn_id, &msg);
}

/* ================================================================
 * Internal: Handle incoming messages from lobby
 * ================================================================ */

static void ll_handle_message(const NetMsg *msg)
{
    switch (msg->type) {
    case NET_MSG_SERVER_CREATE_ROOM: {
        const NetMsgServerCreateRoom *cr = &msg->server_create_room;
        printf("[lobby-link] Create room request: code='%.8s'\n", cr->room_code);

        int room_idx = room_create_with_code(cr->room_code);

        /* Send ACK back to lobby */
        NetMsg reply;
        memset(&reply, 0, sizeof(reply));
        reply.type = NET_MSG_SERVER_ROOM_CREATED;
        strncpy(reply.server_room_created.room_code, cr->room_code,
                NET_ROOM_CODE_LEN - 1);
        reply.server_room_created.success = (room_idx >= 0) ? 1 : 0;
        net_socket_send_msg(&g_lobby, g_conn_id, &reply);

        if (room_idx >= 0) {
            printf("[lobby-link] Room '%s' created (index %d)\n",
                   cr->room_code, room_idx);
        } else {
            printf("[lobby-link] Failed to create room '%s'\n", cr->room_code);
        }
        break;
    }
    case NET_MSG_ERROR:
        printf("[lobby-link] Error from lobby: %s\n", msg->error.message);
        break;
    default:
        printf("[lobby-link] Unexpected message type %d from lobby\n", msg->type);
        break;
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

void lobby_link_init(const char *lobby_addr, uint16_t lobby_port,
                     const char *self_addr, uint16_t self_port,
                     uint16_t max_rooms)
{
    net_socket_init(&g_lobby, 1);
    g_conn_id = -1;
    g_registered = false;
    g_initialized = true;
    g_last_heartbeat = 0;
    g_last_reconnect = 0;

    strncpy(g_lobby_addr, lobby_addr, NET_ADDR_LEN - 1);
    g_lobby_addr[NET_ADDR_LEN - 1] = '\0';
    g_lobby_port = lobby_port;
    strncpy(g_self_addr, self_addr, NET_ADDR_LEN - 1);
    g_self_addr[NET_ADDR_LEN - 1] = '\0';
    g_self_port = self_port;
    g_max_rooms = max_rooms;

    printf("[lobby-link] Initialized (lobby=%s:%d, self=%s:%d)\n",
           g_lobby_addr, g_lobby_port, g_self_addr, g_self_port);
}

bool lobby_link_connect(void)
{
    if (!g_initialized) return false;

    g_conn_id = net_socket_connect(&g_lobby, g_lobby_addr, g_lobby_port);
    if (g_conn_id < 0) {
        fprintf(stderr, "[lobby-link] Connect to %s:%d failed\n",
                g_lobby_addr, g_lobby_port);
        return false;
    }
    g_registered = false;
    printf("[lobby-link] Connecting to lobby %s:%d...\n",
           g_lobby_addr, g_lobby_port);
    return true;
}

void lobby_link_update(void)
{
    if (!g_initialized) return;

    double now = ll_time_now();

    net_socket_update(&g_lobby);

    /* Check connection state */
    if (g_conn_id >= 0) {
        NetConnState state = net_socket_state(&g_lobby, g_conn_id);

        if (state == NET_CONN_CONNECTED) {
            /* Send registration on first connected tick */
            if (!g_registered) {
                ll_send_register();
                g_last_heartbeat = now;
            }

            /* Process incoming messages */
            NetMsg msg;
            while (net_socket_recv_msg(&g_lobby, g_conn_id, &msg)) {
                ll_handle_message(&msg);
            }

            /* Send periodic heartbeat */
            if (now - g_last_heartbeat >= HEARTBEAT_INTERVAL) {
                ll_send_heartbeat();
                g_last_heartbeat = now;
            }
        } else if (state == NET_CONN_DISCONNECTED) {
            printf("[lobby-link] Disconnected from lobby\n");
            g_conn_id = -1;
            g_registered = false;
        }
        /* NET_CONN_CONNECTING: wait for next update */
    }

    /* Attempt reconnect if disconnected */
    if (g_conn_id < 0 && (now - g_last_reconnect) >= RECONNECT_INTERVAL) {
        g_last_reconnect = now;
        lobby_link_connect();
    }
}

void lobby_link_shutdown(void)
{
    if (!g_initialized) return;

    if (g_conn_id >= 0) {
        /* Send disconnect message */
        NetMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = NET_MSG_DISCONNECT;
        msg.disconnect.reason = NET_DISCONNECT_NORMAL;
        net_socket_send_msg(&g_lobby, g_conn_id, &msg);
    }

    net_socket_shutdown(&g_lobby);
    g_initialized = false;
    g_registered = false;
    g_conn_id = -1;
    printf("[lobby-link] Shutdown\n");
}

bool lobby_link_is_connected(void)
{
    return g_initialized && g_conn_id >= 0 &&
           net_socket_state(&g_lobby, g_conn_id) == NET_CONN_CONNECTED;
}

void lobby_link_send_result(const char *room_code,
                            const int16_t scores[NET_MAX_PLAYERS],
                            const uint8_t winner_seats[NET_MAX_PLAYERS],
                            int winner_count,
                            int rounds_played,
                            const uint8_t player_tokens[][NET_AUTH_TOKEN_LEN])
{
    if (!lobby_link_is_connected()) {
        printf("[lobby-link] Not connected, dropping result for '%.8s'\n",
               room_code);
        return;
    }

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_SERVER_RESULT;
    strncpy(msg.server_result.room_code, room_code, NET_ROOM_CODE_LEN - 1);
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        msg.server_result.final_scores[i] = scores[i];
        msg.server_result.winner_seats[i] = winner_seats[i];
        memcpy(msg.server_result.player_tokens[i], player_tokens[i],
               NET_AUTH_TOKEN_LEN);
    }
    msg.server_result.winner_count = (uint8_t)winner_count;
    msg.server_result.rounds_played = (uint16_t)rounds_played;
    net_socket_send_msg(&g_lobby, g_conn_id, &msg);

    printf("[lobby-link] Sent result for room '%.8s' (%d rounds, %d winner(s))\n",
           room_code, rounds_played, winner_count);
}
