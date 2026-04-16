/* ============================================================
 * @deps-implements: lobby/lobby_net.h
 * @deps-requires: lobby/lobby_net.h, lobby/db.h, lobby/auth.h,
 *                 lobby/rooms.h, lobby/server_registry.h,
 *                 lobby/matchmaking.h, lobby/stats.h (stats_calc_elo_deltas),
 *                 lobby/friends.h,
 *                 net/socket.h, net/protocol.h (NET_MSG_SERVER_ROOM_DESTROYED, NetMsgServerRoomDestroyed),
 *                 stdio.h, stdlib.h, string.h, time.h
 * @deps-last-changed: 2026-04-06 — Task 5: Friends dispatch, auth/disconnect hooks, room lifecycle hooks
 * ============================================================ */

#include "lobby_net.h"

#include <math.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "auth.h"
#include "db.h"
#include "friends.h"
#include "matchmaking.h"
#include "rooms.h"
#include "stats.h"
#include "server_registry.h"
#include "net/socket.h"
#include "net/protocol.h"
#include "net/version.h"

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
    char           pending_username[32];               /* username for login_ack */
    bool           hello_ok;                            /* client passed version check */
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
static void lby_handle_login_by_key(int conn_id, const NetMsgLoginByKey *msg);
static void lby_handle_login_response(int conn_id, const NetMsgLoginResponse *resp);
static void lby_handle_logout(int conn_id);

/* Room handlers (Step 16) */
static void lby_handle_create_room(int conn_id, const NetMsgCreateRoom *cr);
static void lby_handle_join_room(int conn_id, const NetMsgJoinRoom *jr);
static void lby_handle_server_room_created(int conn_id, const NetMsgServerRoomCreated *rc);
static void lby_handle_server_room_destroyed(int conn_id, const NetMsgServerRoomDestroyed *rd);

/* Username change handler (Step 19) */
static void lby_handle_change_username(int conn_id,
                                       const NetMsgChangeUsername *cu);

/* Matchmaking handlers (Step 17) */
static void lby_handle_queue(int conn_id, const NetMsgQueueMatchmake *qm);
static void lby_handle_queue_cancel(int conn_id);
static void lby_process_match(const MmMatchResult *match);

/* Server registry handlers (Step 16) */
static void lby_handle_server_register(int conn_id, const NetMsgServerRegister *sr);
static void lby_handle_server_result(int conn_id, const NetMsgServerResult *res);
static void lby_handle_server_heartbeat(int conn_id, const NetMsgServerHeartbeat *hb);

/* Stats & Leaderboard handlers */
static void lby_handle_stats_request(int conn_id, const NetMsgStatsRequest *req);
static void lby_handle_leaderboard_request(int conn_id, const NetMsgLeaderboardRequest *req);

/* Pending timeout callbacks */
static void lby_on_pending_timeout(int client_conn_id);
static void lby_on_mm_pending_timeout(const int conn_ids[MM_PLAYERS_PER_MATCH]);

/* Dead server callback */
static void lby_on_dead_server(int conn_id);

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
    mm_init();
    friends_init(&g_net);
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

    /* Stage 5a: Expire matchmaking pending matches every tick (cheap O(16) scan) */
    {
        double now = lby_time_now();
        mm_pending_expire(now, MM_PENDING_TIMEOUT, lby_on_mm_pending_timeout);

        /* Stage 5b: Periodic maintenance (~30s) */
        if (now - g_last_maintenance > 30.0) {
            g_last_maintenance = now;
            lobby_rooms_cleanup(g_db);
            lobby_pending_expire(now, lby_on_pending_timeout);
            svreg_expire_dead(g_db, now, SVREG_HEARTBEAT_TIMEOUT,
                              lby_on_dead_server);
            auth_cleanup_expired(g_db);
        }
    }
}

int lobby_net_find_conn_by_account(int32_t account_id)
{
    for (int i = 0; i < g_net.max_conns; i++) {
        if (net_socket_state(&g_net, i) != NET_CONN_CONNECTED) continue;
        LobbyConnInfo *ci = g_net.conns[i].user_data;
        if (ci && ci->auth_state == LOBBY_AUTH_AUTHENTICATED &&
            ci->account_id == account_id)
            return i;
    }
    return -1;
}

/* ================================================================
 * Message Routing
 * ================================================================ */

static void lby_send_version_reject(int conn_id)
{
    NetMsg reply = {0};
    reply.type = NET_MSG_HANDSHAKE_REJECT;
    reply.handshake_reject.server_version = PROTOCOL_VERSION;
    reply.handshake_reject.reason         = NET_REJECT_VERSION_MISMATCH;
    net_socket_send_msg(&g_net, conn_id, &reply);
}

static bool lby_is_server_msg(NetMsgType t)
{
    return t == NET_MSG_SERVER_REGISTER ||
           t == NET_MSG_SERVER_RESULT ||
           t == NET_MSG_SERVER_HEARTBEAT ||
           t == NET_MSG_SERVER_ROOM_CREATED ||
           t == NET_MSG_SERVER_ROOM_DESTROYED;
}

static void lby_handle_message(int conn_id, const NetMsg *msg)
{
    /* Version handshake: clients must send NET_MSG_LOBBY_HELLO first.
     * Game server connections bypass this (identified by NET_MSG_SERVER_*). */
    LobbyConnInfo *gate_info = g_net.conns[conn_id].user_data;
    if (msg->type == NET_MSG_LOBBY_HELLO) {
        if (!gate_info) return;
        if (strncmp(msg->lobby_hello.version, HH_VERSION,
                    sizeof(msg->lobby_hello.version)) != 0) {
            printf("[lobby-net] Conn %d version mismatch: client='%.31s' server='%s'\n",
                   conn_id, msg->lobby_hello.version, HH_VERSION);
            lby_send_version_reject(conn_id);
            lby_cleanup_connection(conn_id);
            return;
        }
        gate_info->hello_ok = true;
        gate_info->type     = LOBBY_CONN_CLIENT;
        return;
    }
    if (gate_info && !gate_info->hello_ok && !lby_is_server_msg(msg->type) &&
        msg->type != NET_MSG_PING && msg->type != NET_MSG_DISCONNECT) {
        printf("[lobby-net] Conn %d sent msg %d before hello — rejecting\n",
               conn_id, msg->type);
        lby_send_version_reject(conn_id);
        lby_cleanup_connection(conn_id);
        return;
    }

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
    case NET_MSG_LOGIN_BY_KEY:
        lby_handle_login_by_key(conn_id, &msg->login_by_key);
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
    case NET_MSG_CHANGE_USERNAME:
        lby_handle_change_username(conn_id, &msg->change_username);
        break;
    case NET_MSG_STATS_REQUEST:
        lby_handle_stats_request(conn_id, &msg->stats_request);
        break;
    case NET_MSG_LEADERBOARD_REQUEST:
        lby_handle_leaderboard_request(conn_id, &msg->leaderboard_request);
        break;

    /* Friend system messages */
    case NET_MSG_FRIEND_SEARCH: {
        LobbyConnInfo *ci = g_net.conns[conn_id].user_data;
        if (ci && ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_search(conn_id, ci->account_id, &msg->friend_search, g_db);
        break;
    }
    case NET_MSG_FRIEND_REQUEST: {
        LobbyConnInfo *ci = g_net.conns[conn_id].user_data;
        if (ci && ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_request(conn_id, ci->account_id, &msg->friend_request, g_db);
        break;
    }
    case NET_MSG_FRIEND_ACCEPT: {
        LobbyConnInfo *ci = g_net.conns[conn_id].user_data;
        if (ci && ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_accept(conn_id, ci->account_id, &msg->friend_accept, g_db);
        break;
    }
    case NET_MSG_FRIEND_REJECT: {
        LobbyConnInfo *ci = g_net.conns[conn_id].user_data;
        if (ci && ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_reject(conn_id, ci->account_id, &msg->friend_reject, g_db);
        break;
    }
    case NET_MSG_FRIEND_REMOVE: {
        LobbyConnInfo *ci = g_net.conns[conn_id].user_data;
        if (ci && ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_remove(conn_id, ci->account_id, &msg->friend_remove, g_db);
        break;
    }
    case NET_MSG_FRIEND_LIST_REQUEST: {
        LobbyConnInfo *ci = g_net.conns[conn_id].user_data;
        if (ci && ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_list_request(conn_id, ci->account_id, g_db);
        break;
    }
    case NET_MSG_ROOM_INVITE: {
        LobbyConnInfo *ci = g_net.conns[conn_id].user_data;
        if (ci && ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_room_invite(conn_id, ci->account_id, &msg->room_invite, g_db);
        break;
    }

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
    case NET_MSG_SERVER_ROOM_DESTROYED:
        lby_handle_server_room_destroyed(conn_id, &msg->server_room_destroyed);
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
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        reply.pong.server_timestamp_ms =
            (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    }
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
            mm_queue_remove(conn_id);
            /* If player was in a pending match, notify the other players */
            MmPendingMatch removed_match;
            if (mm_pending_remove_by_conn(conn_id, &removed_match)) {
                for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
                    if (removed_match.conn_ids[i] == conn_id) continue;
                    if (net_socket_state(&g_net, removed_match.conn_ids[i])
                        == NET_CONN_CONNECTED) {
                        lby_send_error(removed_match.conn_ids[i], 25,
                                       "A matched player disconnected");
                    }
                }
            }
        } else if (info->type == LOBBY_CONN_GAME_SERVER) {
            type_str = "game-server";
            svreg_unregister(g_db, conn_id);
            lobby_pending_remove_by_server(conn_id);
            mm_pending_remove_by_server(conn_id);
        }
        if (info->auth_state == LOBBY_AUTH_AUTHENTICATED && info->account_id > 0)
            friends_on_player_disconnected(info->account_id, g_db);
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
        lby_send_error(conn_id, 3, "Invalid username (4-31 chars, alphanumeric + underscore)");
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
        printf("[lobby-net] Login failed for conn %d (user='%.32s'): %s\n",
               conn_id, login->username,
               r == AUTH_ERR_UNKNOWN_USER ? "unknown user" : "db error");
        lby_send_error(conn_id, 4, "Unknown username");
        return;
    }

    /* Store state for challenge verification */
    info->account_id = account_id;
    memcpy(info->pending_pk, pk, AUTH_PK_LEN);
    snprintf(info->pending_username, sizeof(info->pending_username),
             "%s", login->username);
    if (!auth_generate_challenge(info->challenge_nonce)) {
        fprintf(stderr, "[lobby-net] RNG failure generating challenge for conn %d\n",
                conn_id);
        NetMsg err;
        memset(&err, 0, sizeof(err));
        err.type = NET_MSG_ERROR;
        snprintf(err.error.message, NET_MAX_CHAT_LEN,
                 "Internal server error");
        net_socket_send_msg(&g_net, conn_id, &err);
        return;
    }
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

static void lby_handle_login_by_key(int conn_id, const NetMsgLoginByKey *msg)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (!info) return;
    info->type = LOBBY_CONN_CLIENT;

    if (info->auth_state == LOBBY_AUTH_CHALLENGE_SENT) {
        lby_send_error(conn_id, 7, "Login already in progress");
        return;
    }

    /* Look up account by public key */
    int32_t account_id;
    char username[32];
    AuthResult r = auth_find_account_by_key(g_db, msg->public_key,
                                            &account_id, username, sizeof(username));
    if (r != AUTH_OK) {
        printf("[lobby-net] Login-by-key failed for conn %d: unknown key\n", conn_id);
        lby_send_error(conn_id, 4, "No account for this key");
        return;
    }

    /* Store state for challenge verification */
    info->account_id = account_id;
    memcpy(info->pending_pk, msg->public_key, AUTH_PK_LEN);
    snprintf(info->pending_username, sizeof(info->pending_username),
             "%s", username);
    if (!auth_generate_challenge(info->challenge_nonce)) {
        fprintf(stderr, "[lobby-net] RNG failure for conn %d\n", conn_id);
        lby_send_error(conn_id, 10, "Internal server error");
        return;
    }
    info->auth_state = LOBBY_AUTH_CHALLENGE_SENT;

    /* Send challenge */
    NetMsg reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = NET_MSG_LOGIN_CHALLENGE;
    memcpy(reply.login_challenge.nonce, info->challenge_nonce, AUTH_CHALLENGE_LEN);
    net_socket_send_msg(&g_net, conn_id, &reply);

    printf("[lobby-net] Sent login challenge to conn %d (key-login, user='%s')\n",
           conn_id, username);
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
    strncpy(reply.login_ack.username, info->pending_username,
            NET_MAX_NAME_LEN - 1);
    net_socket_send_msg(&g_net, conn_id, &reply);
    friends_on_player_authenticated(conn_id, info->account_id, g_db);

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
 * Username Change Handler (Step 19)
 * ================================================================ */

static void lby_handle_change_username(int conn_id,
                                       const NetMsgChangeUsername *cu)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (!info || info->auth_state != LOBBY_AUTH_AUTHENTICATED) {
        lby_send_error(conn_id, 20, "Not authenticated");
        return;
    }

    /* Validate token matches this connection */
    if (memcmp(cu->auth_token, info->session_token, AUTH_TOKEN_LEN) != 0) {
        lby_send_error(conn_id, 20, "Invalid token");
        return;
    }

    AuthResult r = auth_change_username(g_db, info->account_id,
                                        cu->new_username);
    switch (r) {
    case AUTH_OK:
        printf("[lobby-net] Username changed for account %d -> '%s'\n",
               info->account_id, cu->new_username);
        /* Send a generic ACK (reuse REGISTER_ACK — no payload) */
        {
            NetMsg ack;
            memset(&ack, 0, sizeof(ack));
            ack.type = NET_MSG_REGISTER_ACK;
            net_socket_send_msg(&g_net, conn_id, &ack);
        }
        break;
    case AUTH_ERR_USERNAME_TAKEN:
        lby_send_error(conn_id, 1, "Username already taken");
        break;
    case AUTH_ERR_INVALID_INPUT:
        lby_send_error(conn_id, 3,
                       "Invalid username (4-31 chars, alphanumeric + underscore)");
        break;
    default:
        lby_send_error(conn_id, 10, "Username change failed");
        break;
    }
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

    /* Check if this is a matchmade room first */
    int mm_idx = mm_pending_find_by_code(rc->room_code);
    if (mm_idx >= 0) {
        const MmPendingMatch *pm = mm_pending_get(mm_idx);
        if (!pm) {
            mm_pending_remove(mm_idx);
            return;
        }

        /* Copy conn_ids before removing pending entry */
        int match_conns[MM_PLAYERS_PER_MATCH];
        memcpy(match_conns, pm->conn_ids, sizeof(match_conns));
        mm_pending_remove(mm_idx);

        if (!rc->success) {
            printf("[lobby-net] Server failed to create matchmade room '%.8s'\n",
                   rc->room_code);
            lobby_rooms_set_status(g_db, rc->room_code, "expired");
            for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
                if (net_socket_state(&g_net, match_conns[i]) == NET_CONN_CONNECTED)
                    lby_send_error(match_conns[i], 27,
                                   "Game server failed to create room");
            }
            return;
        }

        /* Look up room to get server address */
        char addr[NET_ADDR_LEN];
        uint16_t port;
        char status[16];
        if (!lobby_rooms_lookup(g_db, rc->room_code, addr, &port, status)) {
            for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
                if (net_socket_state(&g_net, match_conns[i]) == NET_CONN_CONNECTED)
                    lby_send_error(match_conns[i], 28, "Room data lost");
            }
            return;
        }

        /* Send ROOM_ASSIGNED to all 4 matched players */
        for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
            int cc = match_conns[i];
            if (net_socket_state(&g_net, cc) != NET_CONN_CONNECTED) {
                printf("[lobby-net] Matched player conn %d gone\n", cc);
                continue;
            }
            LobbyConnInfo *ci = g_net.conns[cc].user_data;
            if (!ci) continue;

            NetMsg reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = NET_MSG_ROOM_ASSIGNED;
            strncpy(reply.room_assigned.server_addr, addr, NET_ADDR_LEN - 1);
            reply.room_assigned.server_port = port;
            strncpy(reply.room_assigned.room_code, rc->room_code,
                    NET_ROOM_CODE_LEN - 1);
            memcpy(reply.room_assigned.auth_token, ci->session_token,
                   AUTH_TOKEN_LEN);
            net_socket_send_msg(&g_net, cc, &reply);
        }
        /* Notify friends module of new room */
        {
            int32_t account_ids[MM_PLAYERS_PER_MATCH];
            int player_count = 0;
            for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
                LobbyConnInfo *ci = g_net.conns[match_conns[i]].user_data;
                if (ci && ci->auth_state == LOBBY_AUTH_AUTHENTICATED &&
                    ci->account_id > 0)
                    account_ids[player_count++] = ci->account_id;
            }
            friends_on_room_created(rc->room_code, account_ids, player_count, g_db);
        }
        printf("[lobby-net] Matchmade room '%.8s' created -> %s:%d "
               "(4 players notified)\n", rc->room_code, addr, port);
        return;
    }

    /* Private room (existing logic) */
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

    /* Notify friends module of new room */
    if (info->auth_state == LOBBY_AUTH_AUTHENTICATED && info->account_id > 0) {
        int32_t account_ids[1] = { info->account_id };
        friends_on_room_created(rc->room_code, account_ids, 1, g_db);
    }

    printf("[lobby-net] Room '%.8s' created, assigned to client conn %d -> %s:%d\n",
           rc->room_code, client_conn, addr, port);
}

static void lby_handle_server_room_destroyed(int conn_id,
                                             const NetMsgServerRoomDestroyed *rd)
{
    LobbyConnInfo *sender = g_net.conns[conn_id].user_data;
    if (!sender || sender->type != LOBBY_CONN_GAME_SERVER) {
        lby_send_error(conn_id, 31, "Not a game server");
        return;
    }

    lobby_rooms_set_status(g_db, rd->room_code, "expired");
    friends_on_room_destroyed(rd->room_code, g_db);
    friends_expire_room_invites(rd->room_code);

    int idx = lobby_pending_find_by_code(rd->room_code);
    if (idx >= 0)
        lobby_pending_remove(idx);

    printf("[lobby-net] Room '%.4s' destroyed by game server\n", rd->room_code);
}

/* ================================================================
 * Matchmaking Handlers (Step 17)
 * ================================================================ */

/* Try to create a room for a formed match. Called after mm_try_form_match()
 * returns a successful result. On failure, sends errors to all 4 players. */
static void lby_process_match(const MmMatchResult *match)
{
    /* Pick a game server */
    const RegisteredServer *srv = svreg_pick_server();
    if (!srv) {
        for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
            if (net_socket_state(&g_net, match->conn_ids[i]) == NET_CONN_CONNECTED)
                lby_send_error(match->conn_ids[i], 21,
                               "No game servers available");
        }
        return;
    }

    /* Verify server connection */
    if (net_socket_state(&g_net, srv->conn_id) != NET_CONN_CONNECTED) {
        for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
            if (net_socket_state(&g_net, match->conn_ids[i]) == NET_CONN_CONNECTED)
                lby_send_error(match->conn_ids[i], 23,
                               "Game server disconnected");
        }
        return;
    }

    /* Generate room code */
    char code[LOBBY_ROOM_CODE_LEN];
    if (!lobby_rooms_generate_code(g_db, code)) {
        for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
            if (net_socket_state(&g_net, match->conn_ids[i]) == NET_CONN_CONNECTED)
                lby_send_error(match->conn_ids[i], 22,
                               "Failed to generate room code");
        }
        return;
    }

    /* Store in DB */
    if (!lobby_rooms_insert(g_db, code, srv->addr, srv->port)) {
        for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
            if (net_socket_state(&g_net, match->conn_ids[i]) == NET_CONN_CONNECTED)
                lby_send_error(match->conn_ids[i], 23,
                               "Failed to create room");
        }
        return;
    }

    /* Track pending match */
    double now = lby_time_now();
    int pending_idx = mm_pending_add(code, srv->conn_id,
                                     match->conn_ids, match->account_ids,
                                     now);
    if (pending_idx < 0) {
        lobby_rooms_set_status(g_db, code, "expired");
        for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
            if (net_socket_state(&g_net, match->conn_ids[i]) == NET_CONN_CONNECTED)
                lby_send_error(match->conn_ids[i], 24,
                               "Too many pending requests");
        }
        return;
    }

    /* Verify all 4 matched players are still connected before creating room */
    for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
        int cc = match->conn_ids[i];
        LobbyConnInfo *ci = (net_socket_state(&g_net, cc) == NET_CONN_CONNECTED)
                            ? g_net.conns[cc].user_data : NULL;
        if (!ci) {
            printf("[lobby-net] Matched player conn %d disconnected, "
                   "aborting match\n", cc);
            mm_pending_remove(pending_idx);
            lobby_rooms_set_status(g_db, code, "expired");
            for (int j = 0; j < MM_PLAYERS_PER_MATCH; j++) {
                if (j == i) continue;
                if (net_socket_state(&g_net, match->conn_ids[j]) == NET_CONN_CONNECTED)
                    lby_send_error(match->conn_ids[j], 25,
                                   "A matched player disconnected");
            }
            return;
        }
    }

    /* Build SERVER_CREATE_ROOM with all 4 player tokens */
    NetMsg create_msg;
    memset(&create_msg, 0, sizeof(create_msg));
    create_msg.type = NET_MSG_SERVER_CREATE_ROOM;
    strncpy(create_msg.server_create_room.room_code, code, NET_ROOM_CODE_LEN - 1);
    for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
        LobbyConnInfo *ci = g_net.conns[match->conn_ids[i]].user_data;
        memcpy(create_msg.server_create_room.player_tokens[i],
               ci->session_token, AUTH_TOKEN_LEN);
    }
    net_socket_send_msg(&g_net, srv->conn_id, &create_msg);

    printf("[lobby-net] Matchmaking: code=%s, server=%s:%d, "
           "conns [%d,%d,%d,%d]\n",
           code, srv->addr, srv->port,
           match->conn_ids[0], match->conn_ids[1],
           match->conn_ids[2], match->conn_ids[3]);
}

static void lby_handle_queue(int conn_id, const NetMsgQueueMatchmake *qm)
{
    (void)qm;
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (!info || info->auth_state != LOBBY_AUTH_AUTHENTICATED) {
        lby_send_error(conn_id, 20, "Not authenticated");
        return;
    }

    int position = mm_queue_add(conn_id, info->account_id);
    if (position < 0) {
        lby_send_error(conn_id, 40, "Cannot join queue (full or already queued)");
        return;
    }

    /* Send queue status to the player */
    NetMsg status_msg;
    memset(&status_msg, 0, sizeof(status_msg));
    status_msg.type = NET_MSG_QUEUE_STATUS;
    status_msg.queue_status.position = (uint16_t)position;
    status_msg.queue_status.estimated_wait_secs = 0;
    net_socket_send_msg(&g_net, conn_id, &status_msg);

    /* Check if a match can be formed */
    MmMatchResult match = mm_try_form_match();
    if (match.formed) {
        lby_process_match(&match);
    }
}

static void lby_handle_queue_cancel(int conn_id)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (!info || info->auth_state != LOBBY_AUTH_AUTHENTICATED) {
        lby_send_error(conn_id, 20, "Not authenticated");
        return;
    }

    if (mm_queue_remove(conn_id)) {
        printf("[lobby-net] QueueCancel from conn %d\n", conn_id);
    }
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

/* ================================================================
 * Stats & Leaderboard Handlers
 * ================================================================ */

static void lby_handle_stats_request(int conn_id, const NetMsgStatsRequest *req)
{
    int32_t account_id = auth_validate_token(g_db, req->auth_token);
    if (account_id < 0) {
        lby_send_error(conn_id, 1, "Invalid token");
        return;
    }

    NetMsg resp;
    memset(&resp, 0, sizeof(resp));
    resp.type = NET_MSG_STATS_RESPONSE;
    NetMsgStatsResponse *s = &resp.stats_response;

    /* Full stats from stats table */
    sqlite3_stmt *stmt = lobbydb_stmt(g_db, LOBBY_STMT_GET_FULL_STATS);
    if (!stmt) { lby_send_error(conn_id, 2, "DB error"); return; }
    sqlite3_bind_int(stmt, 1, account_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        s->games_played        = (uint32_t)sqlite3_column_int(stmt, 0);
        s->games_won           = (uint32_t)sqlite3_column_int(stmt, 1);
        s->total_score         = sqlite3_column_int(stmt, 2);
        s->elo_rating          = (int32_t)(sqlite3_column_double(stmt, 3));
        s->moon_shots          = (uint32_t)sqlite3_column_int(stmt, 4);
        s->qos_caught          = (uint32_t)sqlite3_column_int(stmt, 5);
        s->contracts_fulfilled = (uint32_t)sqlite3_column_int(stmt, 6);
        s->perfect_rounds      = (uint32_t)sqlite3_column_int(stmt, 7);
        s->hearts_collected    = (uint32_t)sqlite3_column_int(stmt, 8);
        s->tricks_won          = (uint32_t)sqlite3_column_int(stmt, 9);
    }

    /* Derived: best/worst score from match_players */
    stmt = lobbydb_stmt(g_db, LOBBY_STMT_GET_BEST_WORST_SCORE);
    if (stmt) {
        sqlite3_bind_int(stmt, 1, account_id);
        if (sqlite3_step(stmt) == SQLITE_ROW &&
            sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            s->best_score  = sqlite3_column_int(stmt, 0);
            s->worst_score = sqlite3_column_int(stmt, 1);
        }
    }

    /* Derived: avg placement */
    stmt = lobbydb_stmt(g_db, LOBBY_STMT_GET_AVG_PLACEMENT);
    if (stmt) {
        sqlite3_bind_int(stmt, 1, account_id);
        if (sqlite3_step(stmt) == SQLITE_ROW &&
            sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            s->avg_placement_x100 = (uint16_t)(sqlite3_column_double(stmt, 0) * 100.0);
        }
    }

    net_socket_send_msg(&g_net, conn_id, &resp);
}

static void lby_handle_leaderboard_request(int conn_id,
                                           const NetMsgLeaderboardRequest *req)
{
    int32_t account_id = auth_validate_token(g_db, req->auth_token);
    if (account_id < 0) {
        lby_send_error(conn_id, 1, "Invalid token");
        return;
    }

    NetMsg resp;
    memset(&resp, 0, sizeof(resp));
    resp.type = NET_MSG_LEADERBOARD_RESPONSE;
    NetMsgLeaderboardResponse *lb = &resp.leaderboard_response;

    /* Top 100 by ELO */
    sqlite3_stmt *stmt = lobbydb_stmt(g_db, LOBBY_STMT_GET_LEADERBOARD);
    if (stmt) {
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW &&
               count < LEADERBOARD_MAX_ENTRIES) {
            NetLeaderboardEntry *e = &lb->entries[count];
            const char *name = (const char *)sqlite3_column_text(stmt, 0);
            if (name)
                strncpy(e->username, name, NET_MAX_NAME_LEN - 1);
            e->elo_rating   = (int32_t)(sqlite3_column_double(stmt, 1));
            e->games_played = (uint32_t)sqlite3_column_int(stmt, 2);
            e->games_won    = (uint32_t)sqlite3_column_int(stmt, 3);
            count++;
        }
        lb->entry_count = (uint8_t)count;
    }

    /* Player's own rank */
    stmt = lobbydb_stmt(g_db, LOBBY_STMT_GET_PLAYER_RANK);
    if (stmt) {
        sqlite3_bind_int(stmt, 1, account_id);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            lb->player_rank = (uint16_t)sqlite3_column_int(stmt, 0);
    }

    /* Player's own ELO */
    stmt = lobbydb_stmt(g_db, LOBBY_STMT_GET_STATS);
    if (stmt) {
        sqlite3_bind_int(stmt, 1, account_id);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            lb->player_elo = (int32_t)(sqlite3_column_double(stmt, 2));
    }

    net_socket_send_msg(&g_net, conn_id, &resp);
}

static void lby_handle_server_result(int conn_id, const NetMsgServerResult *res)
{
    LobbyConnInfo *info = g_net.conns[conn_id].user_data;
    if (!info || info->type != LOBBY_CONN_GAME_SERVER) return;

    printf("[lobby-net] ServerResult: room='%.8s', rounds=%d, winners=%d\n",
           res->room_code, res->rounds_played, res->winner_count);
    lobby_rooms_set_status(g_db, res->room_code, "finished");

    /* Resolve player tokens to account IDs */
    int32_t account_ids[NET_MAX_PLAYERS];
    int valid_count = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        /* Check for zero token (AI or empty slot) */
        bool is_zero = true;
        for (int j = 0; j < NET_AUTH_TOKEN_LEN; j++) {
            if (res->player_tokens[i][j] != 0) { is_zero = false; break; }
        }
        if (is_zero) {
            account_ids[i] = -1;
            continue;
        }
        account_ids[i] = auth_validate_token(g_db, res->player_tokens[i]);
        if (account_ids[i] >= 0) valid_count++;
    }

    if (valid_count == 0) {
        printf("[lobby-net] No valid players in result, skipping recording\n");
        return;
    }

    /* Begin transaction for atomic match recording */
    if (sqlite3_exec(lobbydb_handle(g_db), "BEGIN", NULL, NULL, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "[lobby-net] Failed to BEGIN transaction: %s\n",
                sqlite3_errmsg(lobbydb_handle(g_db)));
        return;
    }

    /* Insert match_history row */
    sqlite3_stmt *stmt = lobbydb_stmt(g_db, LOBBY_STMT_INSERT_MATCH);
    if (!stmt) {
        fprintf(stderr, "[lobby-net] Failed to get INSERT_MATCH stmt\n");
        sqlite3_exec(lobbydb_handle(g_db), "ROLLBACK", NULL, NULL, NULL);
        return;
    }
    sqlite3_bind_text(stmt, 1, res->room_code, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, res->rounds_played);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "[lobby-net] Failed to insert match_history: %s\n",
                sqlite3_errmsg(lobbydb_handle(g_db)));
        sqlite3_exec(lobbydb_handle(g_db), "ROLLBACK", NULL, NULL, NULL);
        return;
    }
    int64_t match_id = sqlite3_last_insert_rowid(lobbydb_handle(g_db));

    /* Calculate placements (rank by score ascending — lowest is best) */
    int placements[NET_MAX_PLAYERS];
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        int rank = 1;
        for (int j = 0; j < NET_MAX_PLAYERS; j++) {
            if (j != i && res->final_scores[j] < res->final_scores[i])
                rank++;
        }
        placements[i] = rank;
    }

    /* Determine winner set for stats */
    bool is_winner[NET_MAX_PLAYERS] = {false};
    for (int i = 0; i < res->winner_count && i < NET_MAX_PLAYERS; i++) {
        if (res->winner_seats[i] < NET_MAX_PLAYERS)
            is_winner[res->winner_seats[i]] = true;
    }

    /* Insert match_players and update stats for each valid player */
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (account_ids[i] < 0) continue;

        /* Insert match_players row */
        stmt = lobbydb_stmt(g_db, LOBBY_STMT_INSERT_MATCH_PLAYER);
        if (!stmt) goto rollback;
        sqlite3_bind_int64(stmt, 1, match_id);
        sqlite3_bind_int(stmt, 2, account_ids[i]);
        sqlite3_bind_int(stmt, 3, i); /* seat */
        sqlite3_bind_int(stmt, 4, res->final_scores[i]);
        sqlite3_bind_int(stmt, 5, placements[i]);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "[lobby-net] Failed to insert match_player "
                    "seat %d: %s\n", i,
                    sqlite3_errmsg(lobbydb_handle(g_db)));
            goto rollback;
        }

        /* Update stats */
        stmt = lobbydb_stmt(g_db, LOBBY_STMT_UPDATE_STATS);
        if (!stmt) goto rollback;
        sqlite3_bind_int(stmt, 1, is_winner[i] ? 1 : 0);
        sqlite3_bind_int(stmt, 2, res->final_scores[i]);
        sqlite3_bind_int(stmt, 3, account_ids[i]);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "[lobby-net] Failed to update stats "
                    "account %d: %s\n", account_ids[i],
                    sqlite3_errmsg(lobbydb_handle(g_db)));
            goto rollback;
        }

        /* Update extended stats */
        stmt = lobbydb_stmt(g_db, LOBBY_STMT_UPDATE_FULL_STATS);
        if (!stmt) goto rollback;
        sqlite3_bind_int(stmt, 1, res->moon_shots[i]);
        sqlite3_bind_int(stmt, 2, res->qos_caught[i]);
        sqlite3_bind_int(stmt, 3, res->contracts_fulfilled[i]);
        sqlite3_bind_int(stmt, 4, res->perfect_rounds[i]);
        sqlite3_bind_int(stmt, 5, res->hearts_collected[i]);
        sqlite3_bind_int(stmt, 6, res->tricks_won[i]);
        sqlite3_bind_int(stmt, 7, account_ids[i]);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "[lobby-net] Failed to update full stats "
                    "account %d: %s\n", account_ids[i],
                    sqlite3_errmsg(lobbydb_handle(g_db)));
            goto rollback;
        }
    }

    /* ELO rating update (Step 21) */
    double current_elos[NET_MAX_PLAYERS] = {0};
    int elo_placements[NET_MAX_PLAYERS] = {0};
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (account_ids[i] < 0) continue;
        elo_placements[i] = placements[i];
        stmt = lobbydb_stmt(g_db, LOBBY_STMT_GET_STATS);
        if (!stmt) goto rollback;
        sqlite3_bind_int(stmt, 1, account_ids[i]);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            current_elos[i] = sqlite3_column_double(stmt, 2); /* col 2 = elo */
        else
            current_elos[i] = ELO_DEFAULT;
    }

    double elo_deltas[NET_MAX_PLAYERS] = {0};
    stats_calc_elo_deltas(elo_placements, current_elos, elo_deltas);

    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (account_ids[i] < 0) continue;
        double new_elo = current_elos[i] + elo_deltas[i];
        if (new_elo < ELO_MIN) new_elo = ELO_MIN;
        if (new_elo > ELO_MAX) new_elo = ELO_MAX;
        stmt = lobbydb_stmt(g_db, LOBBY_STMT_UPDATE_ELO);
        if (!stmt) goto rollback;
        sqlite3_bind_double(stmt, 1, new_elo);
        sqlite3_bind_int(stmt, 2, account_ids[i]);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "[lobby-net] Failed to update ELO "
                    "account %d: %s\n", account_ids[i],
                    sqlite3_errmsg(lobbydb_handle(g_db)));
            goto rollback;
        }
    }

    if (sqlite3_exec(lobbydb_handle(g_db), "COMMIT", NULL, NULL, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "[lobby-net] Failed to COMMIT transaction: %s\n",
                sqlite3_errmsg(lobbydb_handle(g_db)));
        goto rollback;
    }

    printf("[lobby-net] Recorded match %lld: room='%.8s', %d players, "
           "%d rounds\n", (long long)match_id, res->room_code,
           valid_count, res->rounds_played);

    /* Send ELO results back to game server */
    {
        NetMsg elo_resp;
        memset(&elo_resp, 0, sizeof(elo_resp));
        elo_resp.type = NET_MSG_SERVER_ELO_RESULT;
        strncpy(elo_resp.server_elo_result.room_code, res->room_code,
                NET_ROOM_CODE_LEN - 1);
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            if (account_ids[i] < 0) {
                elo_resp.server_elo_result.prev_elo[i] = -1;
                elo_resp.server_elo_result.new_elo[i]  = -1;
            } else {
                elo_resp.server_elo_result.prev_elo[i] = (int32_t)round(current_elos[i]);
                double ne = current_elos[i] + elo_deltas[i];
                if (ne < ELO_MIN) ne = ELO_MIN;
                if (ne > ELO_MAX) ne = ELO_MAX;
                elo_resp.server_elo_result.new_elo[i] = (int32_t)round(ne);
            }
        }
        net_socket_send_msg(&g_net, conn_id, &elo_resp);
        printf("[lobby-net] Sent ELO results for room '%.8s'\n",
               res->room_code);
    }

    return;

rollback:
    fprintf(stderr, "[lobby-net] Transaction failed, rolling back\n");
    sqlite3_exec(lobbydb_handle(g_db), "ROLLBACK", NULL, NULL, NULL);
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
    if (net_socket_state(&g_net, client_conn_id) == NET_CONN_CONNECTED) {
        lby_send_error(client_conn_id, 29, "Room creation timed out");
    }
}

static void lby_on_mm_pending_timeout(const int conn_ids[MM_PLAYERS_PER_MATCH])
{
    for (int i = 0; i < MM_PLAYERS_PER_MATCH; i++) {
        if (net_socket_state(&g_net, conn_ids[i]) == NET_CONN_CONNECTED) {
            lby_send_error(conn_ids[i], 29,
                           "Matchmaking room creation timed out");
        }
    }
}

static void lby_on_dead_server(int conn_id)
{
    printf("[lobby-net] Dead server detected, cleaning up conn %d\n", conn_id);

    /* Notify clients waiting on private room creation from this server */
    for (int i = 0; i < LOBBY_MAX_PENDING; i++) {
        const PendingRoomRequest *pr = lobby_pending_get(i);
        if (pr && pr->server_conn_id == conn_id) {
            if (net_socket_state(&g_net, pr->client_conn_id)
                == NET_CONN_CONNECTED) {
                lby_send_error(pr->client_conn_id, 30,
                               "Game server went down");
            }
        }
    }

    /* Notify clients waiting on matchmade room creation from this server */
    for (int i = 0; i < MM_MAX_PENDING; i++) {
        const MmPendingMatch *pm = mm_pending_get(i);
        if (pm && pm->server_conn_id == conn_id) {
            for (int j = 0; j < MM_PLAYERS_PER_MATCH; j++) {
                if (net_socket_state(&g_net, pm->conn_ids[j])
                    == NET_CONN_CONNECTED) {
                    lby_send_error(pm->conn_ids[j], 30,
                                   "Game server went down");
                }
            }
        }
    }

    lobby_pending_remove_by_server(conn_id);
    mm_pending_remove_by_server(conn_id);
    if (net_socket_state(&g_net, conn_id) != NET_CONN_DISCONNECTED) {
        net_socket_close(&g_net, conn_id);
    }
}
