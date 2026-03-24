/* ============================================================
 * @deps-implements: lobby/lobby_net.h
 * @deps-requires: lobby/lobby_net.h,
 *                 lobby/db.h (LobbyDB),
 *                 lobby/auth.h (auth_register, auth_find_account,
 *                 auth_generate_challenge, auth_verify_and_login,
 *                 auth_logout, auth_cleanup_expired),
 *                 lobby/rooms.h (lobby_rooms_*, lobby_pending_*),
 *                 lobby/server_registry.h (svreg_*),
 *                 net/socket.h (NetSocket, net_socket_*),
 *                 net/protocol.h (NetMsg, NetMsgType, NET_MAX_CHAT_LEN),
 *                 stdio.h, stdlib.h, string.h, time.h
 * @deps-last-changed: 2026-03-24 — Step 16: Room/server handlers
 * ============================================================ */

#define _POSIX_C_SOURCE 199309L

#include "lobby_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "auth.h"
#include "db.h"
#include "rooms.h"
#include "server_registry.h"
#include "net/socket.h"
#include "net/protocol.h"

/* ================================================================
 * Per-Connection Metadata (stored in NetConn.user_data)
 * ================================================================ */

typedef enum LobbyConnType {
    LOBBY_CONN_UNKNOWN = 0,
    LOBBY_CONN_CLIENT,
    LOBBY_CONN_GAME_SERVER
} LobbyConnType;

typedef enum LobbyAuthState {
    LOBBY_AUTH_NONE = 0,
    LOBBY_AUTH_CHALLENGE_SENT,
    LOBBY_AUTH_AUTHENTICATED
} LobbyAuthState;

typedef struct LobbyConnInfo {
    LobbyConnType  type;
    int32_t        account_id;                        /* -1 if not authenticated */
    LobbyAuthState auth_state;
    uint8_t        challenge_nonce[AUTH_CHALLENGE_LEN]; /* valid when CHALLENGE_SENT */
    uint8_t        pending_pk[AUTH_PK_LEN];            /* stored pk for verification */
    uint8_t        session_token[AUTH_TOKEN_LEN];      /* valid when AUTHENTICATED */
} LobbyConnInfo;

/* ================================================================
 * File-scope state
 * ================================================================ */

static NetSocket  g_net;
static LobbyDB   *g_db;
static bool        g_listening;
static double      g_last_maintenance;

/* ================================================================
 * Forward declarations — internal handlers
 * ================================================================ */

static double lby_time_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void lby_handle_message(int conn_id, const NetMsg *msg);
static void lby_handle_ping(int conn_id, const NetMsgPing *ping);
static void lby_cleanup_connection(int conn_id);

/* Auth handlers */
static void lby_handle_register(int conn_id, const NetMsgRegister *reg);
static void lby_handle_login(int conn_id, const NetMsgLogin *login);
static void lby_handle_login_response(int conn_id, const NetMsgLoginResponse *resp);
static void lby_handle_logout(int conn_id);

/* Room handlers (Step 16) */
static void lby_handle_create_room(int conn_id, const NetMsgCreateRoom *cr);
static void lby_handle_join_room(int conn_id, const NetMsgJoinRoom *jr);
static void lby_handle_server_room_created(int conn_id, const NetMsgServerRoomCreated *rc);

/* Matchmaking stubs (Step 17) */
static void lby_handle_queue(int conn_id, const NetMsgQueueMatchmake *qm);
static void lby_handle_queue_cancel(int conn_id);

/* Server registry handlers (Step 16) */
static void lby_handle_server_register(int conn_id, const NetMsgServerRegister *sr);
static void lby_handle_server_result(int conn_id, const NetMsgServerResult *res);
static void lby_handle_server_heartbeat(int conn_id, const NetMsgServerHeartbeat *hb);

/* Pending timeout callback */
static void lby_on_pending_timeout(int client_conn_id);

/* ================================================================
 * Helpers
 * ================================================================ */

static void lby_send_error(int conn_id, uint8_t code, const char *message)
{
    NetMsg err;
    memset(&err, 0, sizeof(err));
    err.type = NET_MSG_ERROR;
    err.error.code = code;
    snprintf(err.error.message, NET_MAX_CHAT_LEN, "%s", message);
    net_socket_send_msg(&g_net, conn_id, &err);
}

static void lby_send_not_implemented(int conn_id, const char *what)
{
    printf("[lobby-net] %s from conn %d (stub — not implemented)\n",
           what, conn_id);
    NetMsg err;
    memset(&err, 0, sizeof(err));
    err.type = NET_MSG_ERROR;
    err.error.code = 255;
    snprintf(err.error.message, NET_MAX_CHAT_LEN, "%s: not implemented yet", what);
    net_socket_send_msg(&g_net, conn_id, &err);
}

/* ================================================================
 * Public API
 * ================================================================ */

void lobby_net_init(int max_connections, struct LobbyDB *ldb)
{
    net_socket_init(&g_net, max_connections);
    g_db = ldb;
    g_listening = false;
    g_last_maintenance = 0;
    lobby_pending_init();
    svreg_init();
    printf("[lobby-net] Initialized (max %d connections)\n", max_connections);
}

void lobby_net_shutdown(void)
{
    /* Free LobbyConnInfo for all active connections */
    for (int i = 0; i < g_net.max_conns; i++) {
        if (net_socket_state(&g_net, i) != NET_CONN_DISCONNECTED &&
            g_net.conns[i].user_data != NULL) {
            free(g_net.conns[i].user_data);
            g_net.conns[i].user_data = NULL;
        }
    }
    net_socket_shutdown(&g_net);
    g_listening = false;
    g_db = NULL;
    printf("[lobby-net] Shutdown\n");
}

bool lobby_net_listen(uint16_t port)
{
    if (net_socket_listen(&g_net, port) < 0) {
        fprintf(stderr, "[lobby-net] Failed to listen on port %d\n", port);
        return false;
    }
    g_listening = true;
    printf("[lobby-net] Listening on port %d\n", port);
    return true;
}

void lobby_net_update(void)
{
    if (!g_listening) return;

    /* Stage 1: Poll all sockets */
    net_socket_update(&g_net);

    /* Stage 2: Accept new connections */
    int new_conn;
    while ((new_conn = net_socket_accept(&g_net)) >= 0) {
        LobbyConnInfo *info = calloc(1, sizeof(LobbyConnInfo));
        if (!info) {
            fprintf(stderr, "[lobby-net] OOM allocating conn info, rejecting conn %d\n",
                    new_conn);
            net_socket_close(&g_net, new_conn);
            continue;
        }
        info->type = LOBBY_CONN_UNKNOWN;
        info->account_id = -1;
        info->auth_state = LOBBY_AUTH_NONE;
        g_net.conns[new_conn].user_data = info;
        printf("[lobby-net] New connection: conn %d (%s)\n",
               new_conn, g_net.conns[new_conn].remote_addr);
    }

    /* Stage 3: Process incoming messages from all connections */
    for (int i = 0; i < g_net.max_conns; i++) {
        if (net_socket_state(&g_net, i) != NET_CONN_CONNECTED) continue;

        NetMsg msg;
        while (net_socket_recv_msg(&g_net, i, &msg)) {
            lby_handle_message(i, &msg);
        }
    }

    /* Stage 4: Detect and handle disconnected connections */
    for (int i = 0; i < g_net.max_conns; i++) {
        if (net_socket_state(&g_net, i) == NET_CONN_DISCONNECTED &&
            g_net.conns[i].user_data != NULL) {
            printf("[lobby-net] Connection %d dropped\n", i);
            lby_cleanup_connection(i);
        }
    }

    /* Stage 5: Periodic maintenance (~30s) */
    double now = lby_time_now();
    if (now - g_last_maintenance > 30.0) {
        g_last_maintenance = now;
        lobby_rooms_cleanup(g_db);
        lobby_pending_expire(now, lby_on_pending_timeout);
        auth_cleanup_expired(g_db);
    }
}

/* ================================================================
 * Message Routing
 * ================================================================ */

static void lby_handle_message(int conn_id, const NetMsg *msg)
{
    switch (msg->type) {
    /* Auth messages */
    case NET_MSG_REGISTER:
        lby_handle_register(conn_id, &msg->reg);
        break;
    case NET_MSG_LOGIN:
        lby_handle_login(conn_id, &msg->login);
        break;
    case NET_MSG_LOGIN_RESPONSE:
        lby_handle_login_response(conn_id, &msg->login_response);
        break;
    case NET_MSG_LOGOUT:
        lby_handle_logout(conn_id);
        break;

    /* Room/matchmaking messages (stubs) */
    case NET_MSG_CREATE_ROOM:
        lby_handle_create_room(conn_id, &msg->create_room);
        break;
    case NET_MSG_JOIN_ROOM:
        lby_handle_join_room(conn_id, &msg->join_room);
        break;
    case NET_MSG_QUEUE_MATCHMAKE:
        lby_handle_queue(conn_id, &msg->queue_matchmake);
        break;
    case NET_MSG_QUEUE_CANCEL:
        lby_handle_queue_cancel(conn_id);
        break;

    /* Game Server <-> Lobby messages (stubs) */
    case NET_MSG_SERVER_REGISTER:
        lby_handle_server_register(conn_id, &msg->server_register);
        break;
    case NET_MSG_SERVER_RESULT:
        lby_handle_server_result(conn_id, &msg->server_result);
        break;
    case NET_MSG_SERVER_HEARTBEAT:
        lby_handle_server_heartbeat(conn_id, &msg->server_heartbeat);
        break;
    case NET_MSG_SERVER_ROOM_CREATED:
        lby_handle_server_room_created(conn_id, &msg->server_room_created);
        break;

    /* Common messages */
    case NET_MSG_PING:
        lby_handle_ping(conn_id, &msg->ping);
        break;
    case NET_MSG_DISCONNECT:
        printf("[lobby-net] Client %d sent disconnect\n", conn_id);
        lby_cleanup_connection(conn_id);
        break;

    default:
        printf("[lobby-net] Unexpected message type %d from conn %d\n",
               msg->type, conn_id);
        break;
    }
}

/* ================================================================
 * Ping/Pong
 * ================================================================ */

static void lby_handle_ping(int conn_id, const NetMsgPing *ping)
{
    NetMsg reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = NET_MSG_PONG;
    reply.pong.sequence = ping->sequence;
    reply.pong.echo_timestamp_ms = ping->timestamp_ms;
    reply.pong.server_timestamp_ms = 0; /* TODO: add lobby time */
    net_socket_send_msg(&g_net, conn_id, &reply);
}

/* ================================================================
 * Connection Cleanup
 * ================================================================ */

static void lby_cleanup_connection(int conn_id)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (info) {
        const char *type_str = "unknown";
        if (info->type == LOBBY_CONN_CLIENT) {
            type_str = "client";
            lobby_pending_remove_by_client(conn_id);
        } else if (info->type == LOBBY_CONN_GAME_SERVER) {
            type_str = "game-server";
            svreg_unregister(g_db, conn_id);
            lobby_pending_remove_by_server(conn_id);
        }
        printf("[lobby-net] Cleanup conn %d (type=%s, account=%d)\n",
               conn_id, type_str, info->account_id);
        free(info);
        g_net.conns[conn_id].user_data = NULL;
    }
    net_socket_close(&g_net, conn_id);
}

/* ================================================================
 * Auth Handlers — Registration
 * ================================================================ */

static void lby_handle_register(int conn_id, const NetMsgRegister *reg)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (info) info->type = LOBBY_CONN_CLIENT;

    AuthResult r = auth_register(g_db, reg->username, reg->public_key);

    switch (r) {
    case AUTH_OK: {
        NetMsg reply;
        memset(&reply, 0, sizeof(reply));
        reply.type = NET_MSG_REGISTER_ACK;
        net_socket_send_msg(&g_net, conn_id, &reply);
        break;
    }
    case AUTH_ERR_USERNAME_TAKEN:
        lby_send_error(conn_id, 1, "Username already taken");
        break;
    case AUTH_ERR_PUBKEY_TAKEN:
        lby_send_error(conn_id, 2, "Public key already registered");
        break;
    case AUTH_ERR_INVALID_INPUT:
        lby_send_error(conn_id, 3, "Invalid username (3-31 chars, alphanumeric + underscore)");
        break;
    default:
        lby_send_error(conn_id, 10, "Registration failed (internal error)");
        break;
    }
}

/* ================================================================
 * Auth Handlers — Login (Challenge-Response)
 * ================================================================ */

static void lby_handle_login(int conn_id, const NetMsgLogin *login)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (!info) return;
    info->type = LOBBY_CONN_CLIENT;

    /* Reject if challenge already in progress */
    if (info->auth_state == LOBBY_AUTH_CHALLENGE_SENT) {
        lby_send_error(conn_id, 7, "Login already in progress");
        return;
    }

    /* Look up account */
    int32_t account_id;
    uint8_t pk[AUTH_PK_LEN];
    AuthResult r = auth_find_account(g_db, login->username, &account_id, pk);
    if (r != AUTH_OK) {
        lby_send_error(conn_id, 4, "Unknown username");
        return;
    }

    /* Store state for challenge verification */
    info->account_id = account_id;
    memcpy(info->pending_pk, pk, AUTH_PK_LEN);
    auth_generate_challenge(info->challenge_nonce);
    info->auth_state = LOBBY_AUTH_CHALLENGE_SENT;

    /* Send challenge */
    NetMsg reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = NET_MSG_LOGIN_CHALLENGE;
    memcpy(reply.login_challenge.nonce, info->challenge_nonce, AUTH_CHALLENGE_LEN);
    net_socket_send_msg(&g_net, conn_id, &reply);

    printf("[lobby-net] Sent login challenge to conn %d (user='%.32s')\n",
           conn_id, login->username);
}

static void lby_handle_login_response(int conn_id, const NetMsgLoginResponse *resp)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (!info || info->auth_state != LOBBY_AUTH_CHALLENGE_SENT) {
        lby_send_error(conn_id, 5, "No pending login challenge");
        return;
    }

    uint8_t token[AUTH_TOKEN_LEN];
    AuthAccountInfo acct_info;
    AuthResult r = auth_verify_and_login(
        g_db, info->account_id,
        info->challenge_nonce, resp->signature,
        info->pending_pk, token, &acct_info);

    if (r != AUTH_OK) {
        info->auth_state = LOBBY_AUTH_NONE;
        lby_send_error(conn_id, 6, "Authentication failed (invalid signature)");
        return;
    }

    info->auth_state = LOBBY_AUTH_AUTHENTICATED;
    memcpy(info->session_token, token, AUTH_TOKEN_LEN);

    NetMsg reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = NET_MSG_LOGIN_ACK;
    memcpy(reply.login_ack.auth_token, token, AUTH_TOKEN_LEN);
    reply.login_ack.elo_rating = acct_info.elo_rating;
    reply.login_ack.games_played = acct_info.games_played;
    reply.login_ack.games_won = acct_info.games_won;
    net_socket_send_msg(&g_net, conn_id, &reply);

    printf("[lobby-net] Login success: conn %d -> account %d\n",
           conn_id, info->account_id);
}

/* ================================================================
 * Auth Handlers — Logout
 * ================================================================ */

static void lby_handle_logout(int conn_id)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (info) {
        if (info->auth_state == LOBBY_AUTH_AUTHENTICATED) {
            auth_logout(g_db, info->session_token);
            memset(info->session_token, 0, AUTH_TOKEN_LEN);
        }
        info->auth_state = LOBBY_AUTH_NONE;
        info->account_id = -1;
    }
    printf("[lobby-net] Logout from conn %d\n", conn_id);
}

/* ================================================================
 * Room Handlers (Step 16)
 * ================================================================ */

static void lby_handle_create_room(int conn_id, const NetMsgCreateRoom *cr)
{
    (void)cr;
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (!info || info->auth_state != LOBBY_AUTH_AUTHENTICATED) {
        lby_send_error(conn_id, 20, "Not authenticated");
        return;
    }

    /* Pick a game server */
    const RegisteredServer *srv = svreg_pick_server();
    if (!srv) {
        lby_send_error(conn_id, 21, "No game servers available");
        return;
    }

    /* Generate room code */
    char code[LOBBY_ROOM_CODE_LEN];
    if (!lobby_rooms_generate_code(g_db, code)) {
        lby_send_error(conn_id, 22, "Failed to generate room code");
        return;
    }

    /* Verify server connection is still alive */
    if (net_socket_state(&g_net, srv->conn_id) != NET_CONN_CONNECTED) {
        lby_send_error(conn_id, 23, "Game server disconnected");
        return;
    }

    /* Track pending request BEFORE sending (so we can clean up on failure) */
    double now = lby_time_now();
    int pending_idx = lobby_pending_add(code, conn_id, srv->conn_id, now);
    if (pending_idx < 0) {
        lby_send_error(conn_id, 24, "Too many pending requests");
        return;
    }

    /* Store in DB */
    if (!lobby_rooms_insert(g_db, code, srv->addr, srv->port)) {
        lobby_pending_remove(pending_idx);
        lby_send_error(conn_id, 23, "Failed to create room");
        return;
    }

    /* Tell game server to create the room */
    NetMsg create_msg;
    memset(&create_msg, 0, sizeof(create_msg));
    create_msg.type = NET_MSG_SERVER_CREATE_ROOM;
    strncpy(create_msg.server_create_room.room_code, code, NET_ROOM_CODE_LEN - 1);
    /* Token in slot 0 for the creator */
    memcpy(create_msg.server_create_room.player_tokens[0],
           info->session_token, AUTH_TOKEN_LEN);
    net_socket_send_msg(&g_net, srv->conn_id, &create_msg);

    printf("[lobby-net] CreateRoom: code=%s, server=%s:%d, client conn %d\n",
           code, srv->addr, srv->port, conn_id);
}

static void lby_handle_join_room(int conn_id, const NetMsgJoinRoom *jr)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (!info || info->auth_state != LOBBY_AUTH_AUTHENTICATED) {
        lby_send_error(conn_id, 20, "Not authenticated");
        return;
    }

    /* Look up room code */
    char addr[NET_ADDR_LEN];
    uint16_t port;
    char status[16];
    if (!lobby_rooms_lookup(g_db, jr->room_code, addr, &port, status)) {
        lby_send_error(conn_id, 25, "Room not found");
        return;
    }

    /* Only allow joining active or playing rooms */
    if (strcmp(status, "active") != 0 && strcmp(status, "playing") != 0) {
        lby_send_error(conn_id, 26, "Room is no longer available");
        return;
    }

    /* Send server address to client */
    NetMsg reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = NET_MSG_ROOM_ASSIGNED;
    strncpy(reply.room_assigned.server_addr, addr, NET_ADDR_LEN - 1);
    reply.room_assigned.server_port = port;
    strncpy(reply.room_assigned.room_code, jr->room_code, NET_ROOM_CODE_LEN - 1);
    memcpy(reply.room_assigned.auth_token, info->session_token, AUTH_TOKEN_LEN);
    net_socket_send_msg(&g_net, conn_id, &reply);

    printf("[lobby-net] JoinRoom: code=%.8s -> %s:%d, client conn %d\n",
           jr->room_code, addr, port, conn_id);
}

static void lby_handle_server_room_created(int conn_id,
                                           const NetMsgServerRoomCreated *rc)
{
    /* Only accept from game server connections */
    LobbyConnInfo *sender = g_net.conns[conn_id].user_data;
    if (!sender || sender->type != LOBBY_CONN_GAME_SERVER) {
        lby_send_error(conn_id, 31, "Not a game server");
        return;
    }

    int idx = lobby_pending_find_by_code(rc->room_code);
    if (idx < 0) {
        printf("[lobby-net] ServerRoomCreated for unknown code '%.8s'\n",
               rc->room_code);
        return;
    }

    const PendingRoomRequest *pr = lobby_pending_get(idx);
    if (!pr) {
        lobby_pending_remove(idx);
        return;
    }

    int client_conn = pr->client_conn_id;
    lobby_pending_remove(idx);

    if (!rc->success) {
        printf("[lobby-net] Server failed to create room '%.8s'\n", rc->room_code);
        lobby_rooms_set_status(g_db, rc->room_code, "expired");
        lby_send_error(client_conn, 27, "Game server failed to create room");
        return;
    }

    /* Verify client connection is still alive (may have disconnected during wait) */
    if (net_socket_state(&g_net, client_conn) != NET_CONN_CONNECTED) {
        printf("[lobby-net] Client conn %d gone while waiting for room '%.8s'\n",
               client_conn, rc->room_code);
        return;
    }

    /* Look up the room to get server address */
    char addr[NET_ADDR_LEN];
    uint16_t port;
    char status[16];
    if (!lobby_rooms_lookup(g_db, rc->room_code, addr, &port, status)) {
        lby_send_error(client_conn, 28, "Room data lost");
        return;
    }

    /* Get the client's session token */
    LobbyConnInfo *info = g_net.conns[client_conn].user_data;
    if (!info) return;

    /* Send ROOM_ASSIGNED to the waiting client */
    NetMsg reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = NET_MSG_ROOM_ASSIGNED;
    strncpy(reply.room_assigned.server_addr, addr, NET_ADDR_LEN - 1);
    reply.room_assigned.server_port = port;
    strncpy(reply.room_assigned.room_code, rc->room_code, NET_ROOM_CODE_LEN - 1);
    memcpy(reply.room_assigned.auth_token, info->session_token, AUTH_TOKEN_LEN);
    net_socket_send_msg(&g_net, client_conn, &reply);

    printf("[lobby-net] Room '%.8s' created, assigned to client conn %d -> %s:%d\n",
           rc->room_code, client_conn, addr, port);
}

/* ================================================================
 * Matchmaking Stubs (Step 17)
 * ================================================================ */

static void lby_handle_queue(int conn_id, const NetMsgQueueMatchmake *qm)
{
    (void)qm;
    lby_send_not_implemented(conn_id, "QueueMatchmake");
}

static void lby_handle_queue_cancel(int conn_id)
{
    lby_send_not_implemented(conn_id, "QueueCancel");
}

/* ================================================================
 * Server Registry Handlers (Step 16)
 * ================================================================ */

static void lby_handle_server_register(int conn_id, const NetMsgServerRegister *sr)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (info) info->type = LOBBY_CONN_GAME_SERVER;

    int idx = svreg_register(g_db, conn_id, sr);
    if (idx < 0) {
        lby_send_error(conn_id, 30, "Server registry full");
        return;
    }

    printf("[lobby-net] ServerRegister from conn %d: %s:%d (%d/%d rooms)\n",
           conn_id, sr->addr, sr->port, sr->current_rooms, sr->max_rooms);
}

static void lby_handle_server_result(int conn_id, const NetMsgServerResult *res)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (!info || info->type != LOBBY_CONN_GAME_SERVER) return;
    printf("[lobby-net] ServerResult: room='%.8s', rounds=%d\n",
           res->room_code, res->rounds_played);
    lobby_rooms_set_status(g_db, res->room_code, "finished");
}

static void lby_handle_server_heartbeat(int conn_id, const NetMsgServerHeartbeat *hb)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (!info || info->type != LOBBY_CONN_GAME_SERVER) return;
    svreg_heartbeat(conn_id, hb);
}

/* ================================================================
 * Pending Timeout Callback
 * ================================================================ */

static void lby_on_pending_timeout(int client_conn_id)
{
    lby_send_error(client_conn_id, 29, "Room creation timed out");
}
