/* ============================================================
 * @deps-implements: net/lobby_client.h
 * @deps-requires: net/lobby_client.h (LobbyClientState, LobbyClientInfo),
 *                 net/socket.h (NetSocket, net_socket_*),
 *                 net/protocol.h (NetMsg, NetMsgType, NET_*),
 *                 net/identity.h (Identity, identity_sign),
 *                 stdio.h, string.h
 * @deps-last-changed: 2026-03-27 — Step 23: Added implementations of lobby_client_cancel_create() and lobby_client_cancel_join()
 * ============================================================ */

#include "lobby_client.h"

#include <stdio.h>
#include <string.h>

#include "net/socket.h"

/* ================================================================
 * File-scope state
 * ================================================================ */

static NetSocket        g_net;
static int              g_conn_id;
static LobbyClientState g_state;
static LobbyClientInfo  g_info;
static char             g_error[128];
static bool             g_initialized;

/* Auth state */
static uint8_t g_challenge_nonce[NET_CHALLENGE_LEN];
static char    g_login_username[NET_MAX_NAME_LEN];

/* Connection params for reconnect */
static char     g_lobby_addr[NET_ADDR_LEN];
static uint16_t g_lobby_port;

/* Room assignment (from NET_MSG_ROOM_ASSIGNED) */
static char     g_assigned_addr[NET_ADDR_LEN];
static uint16_t g_assigned_port;
static char     g_assigned_room_code[NET_ROOM_CODE_LEN];
static uint8_t  g_assigned_token[NET_AUTH_TOKEN_LEN];
static bool     g_room_assigned;

/* Stats & Leaderboard response storage */
static PlayerFullStats g_full_stats;
static bool            g_stats_ready;
static LeaderboardData g_leaderboard;
static bool            g_leaderboard_ready;

/* Friend system receive buffers */
static bool                     g_has_friend_list = false;
static NetMsgFriendList         g_friend_list;
static bool                     g_has_search_result = false;
static NetMsgFriendSearchResult g_search_result;

#define MAX_FRIEND_UPDATES 16
static NetMsgFriendUpdate       g_friend_updates[MAX_FRIEND_UPDATES];
static int                      g_friend_update_count = 0;

#define MAX_FRIEND_REQ_NOTIFS 10
static NetMsgFriendRequestNotify g_friend_req_notifs[MAX_FRIEND_REQ_NOTIFS];
static int                       g_friend_req_notif_count = 0;

static bool                     g_has_room_invite = false;
static NetMsgRoomInviteNotify   g_room_invite;

static bool                     g_has_room_invite_expired = false;
static NetMsgRoomInviteExpired  g_room_invite_expired;

/* ================================================================
 * Internal: Handle incoming messages
 * ================================================================ */

static void lc_handle_message(const NetMsg *msg, const Identity *id)
{
    switch (msg->type) {
    case NET_MSG_REGISTER_ACK:
        printf("[lobby-client] Registration successful\n");
        /* Auto-login after registration */
        if (g_state == LOBBY_REGISTERING) {
            lobby_client_login(g_login_username);
        }
        break;

    case NET_MSG_LOGIN_CHALLENGE:
        if (g_state != LOBBY_LOGGING_IN) {
            printf("[lobby-client] Unexpected login challenge\n");
            break;
        }
        /* Sign the nonce */
        memcpy(g_challenge_nonce, msg->login_challenge.nonce,
               NET_CHALLENGE_LEN);
        uint8_t signature[NET_ED25519_SIG_LEN];
        if (!identity_sign(id, g_challenge_nonce, NET_CHALLENGE_LEN,
                           signature)) {
            snprintf(g_error, sizeof(g_error), "Failed to sign challenge");
            g_state = LOBBY_ERROR;
            break;
        }
        /* Send signed response */
        NetMsg resp;
        memset(&resp, 0, sizeof(resp));
        resp.type = NET_MSG_LOGIN_RESPONSE;
        memcpy(resp.login_response.signature, signature,
               NET_ED25519_SIG_LEN);
        net_socket_send_msg(&g_net, g_conn_id, &resp);
        g_state = LOBBY_CHALLENGED;
        printf("[lobby-client] Sent login response\n");
        break;

    case NET_MSG_LOGIN_ACK:
        if (g_state != LOBBY_CHALLENGED) {
            printf("[lobby-client] Unexpected login ACK\n");
            break;
        }
        memcpy(g_info.auth_token, msg->login_ack.auth_token,
               NET_AUTH_TOKEN_LEN);
        g_info.elo_rating = msg->login_ack.elo_rating;
        g_info.games_played = msg->login_ack.games_played;
        g_info.games_won = msg->login_ack.games_won;
        /* Prefer server-provided username (key-based login), fall back to local */
        if (msg->login_ack.username[0] != '\0') {
            strncpy(g_info.username, msg->login_ack.username,
                    NET_MAX_NAME_LEN - 1);
        } else {
            strncpy(g_info.username, g_login_username, NET_MAX_NAME_LEN - 1);
        }
        g_info.username[NET_MAX_NAME_LEN - 1] = '\0';
        g_state = LOBBY_AUTHENTICATED;
        printf("[lobby-client] Authenticated as '%s' "
               "(ELO=%d, played=%u, won=%u)\n",
               g_info.username, g_info.elo_rating,
               g_info.games_played, g_info.games_won);
        break;

    case NET_MSG_ROOM_ASSIGNED:
        strncpy(g_assigned_addr, msg->room_assigned.server_addr,
                NET_ADDR_LEN - 1);
        g_assigned_addr[NET_ADDR_LEN - 1] = '\0';
        g_assigned_port = msg->room_assigned.server_port;
        strncpy(g_assigned_room_code, msg->room_assigned.room_code,
                NET_ROOM_CODE_LEN - 1);
        g_assigned_room_code[NET_ROOM_CODE_LEN - 1] = '\0';
        memcpy(g_assigned_token, msg->room_assigned.auth_token,
               NET_AUTH_TOKEN_LEN);
        g_room_assigned = true;
        g_state = LOBBY_AUTHENTICATED;
        printf("[lobby-client] Room assigned: %s:%d code='%s'\n",
               g_assigned_addr, g_assigned_port, g_assigned_room_code);
        break;

    case NET_MSG_QUEUE_STATUS:
        printf("[lobby-client] Queue position: %d\n",
               msg->queue_status.position);
        break;

    case NET_MSG_ERROR:
        snprintf(g_error, sizeof(g_error), "%s", msg->error.message);
        /* Stay authenticated if we were in a room/queue operation */
        if (g_state == LOBBY_CREATING_ROOM || g_state == LOBBY_JOINING_ROOM ||
            g_state == LOBBY_QUEUED) {
            g_state = LOBBY_AUTHENTICATED;
        } else if (g_state == LOBBY_REGISTERING) {
            /* Registration failed (e.g. pubkey already taken) — try login
             * instead since the key is already registered. */
            printf("[lobby-client] Registration failed (%s), trying login\n",
                   g_error);
            g_state = LOBBY_CONNECTED; /* allow lobby_client_login guard */
            g_error[0] = '\0';
            lobby_client_login(g_login_username);
            printf("[lobby-client] After auto-login: state=%d conn=%d\n",
                   g_state, g_conn_id);
        } else {
            g_state = LOBBY_ERROR;
        }
        printf("[lobby-client] Error: %s\n", g_error);
        break;

    case NET_MSG_STATS_RESPONSE: {
        const NetMsgStatsResponse *s = &msg->stats_response;
        g_full_stats.elo_rating          = s->elo_rating;
        g_full_stats.games_played        = s->games_played;
        g_full_stats.games_won           = s->games_won;
        g_full_stats.total_score         = s->total_score;
        g_full_stats.moon_shots          = s->moon_shots;
        g_full_stats.qos_caught          = s->qos_caught;
        g_full_stats.contracts_fulfilled = s->contracts_fulfilled;
        g_full_stats.perfect_rounds      = s->perfect_rounds;
        g_full_stats.hearts_collected    = s->hearts_collected;
        g_full_stats.tricks_won          = s->tricks_won;
        g_full_stats.best_score          = s->best_score;
        g_full_stats.worst_score         = s->worst_score;
        g_full_stats.avg_placement       = (float)s->avg_placement_x100 / 100.0f;
        g_stats_ready = true;
        break;
    }

    case NET_MSG_LEADERBOARD_RESPONSE: {
        const NetMsgLeaderboardResponse *lb = &msg->leaderboard_response;
        g_leaderboard.count       = lb->entry_count;
        g_leaderboard.player_rank = lb->player_rank;
        g_leaderboard.player_elo  = lb->player_elo;
        for (int i = 0; i < lb->entry_count; i++) {
            memcpy(g_leaderboard.entries[i].username,
                   lb->entries[i].username, NET_MAX_NAME_LEN);
            g_leaderboard.entries[i].elo_rating   = lb->entries[i].elo_rating;
            g_leaderboard.entries[i].games_played = lb->entries[i].games_played;
            g_leaderboard.entries[i].games_won    = lb->entries[i].games_won;
        }
        g_leaderboard_ready = true;
        break;
    }

    case NET_MSG_FRIEND_LIST:
        g_friend_list = msg->friend_list;
        g_has_friend_list = true;
        break;

    case NET_MSG_FRIEND_SEARCH_RESULT:
        g_search_result = msg->friend_search_result;
        g_has_search_result = true;
        break;

    case NET_MSG_FRIEND_UPDATE:
        if (g_friend_update_count < MAX_FRIEND_UPDATES)
            g_friend_updates[g_friend_update_count++] = msg->friend_update;
        break;

    case NET_MSG_FRIEND_REQUEST_NOTIFY:
        if (g_friend_req_notif_count < MAX_FRIEND_REQ_NOTIFS)
            g_friend_req_notifs[g_friend_req_notif_count++] = msg->friend_request_notify;
        break;

    case NET_MSG_ROOM_INVITE_NOTIFY:
        g_room_invite = msg->room_invite_notify;
        g_has_room_invite = true;
        break;

    case NET_MSG_ROOM_INVITE_EXPIRED:
        g_room_invite_expired = msg->room_invite_expired;
        g_has_room_invite_expired = true;
        break;

    default:
        printf("[lobby-client] Unexpected message type %d\n", msg->type);
        break;
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

void lobby_client_init(void)
{
    net_socket_init(&g_net, 1);
    g_conn_id = -1;
    g_state = LOBBY_DISCONNECTED;
    g_initialized = true;
    memset(&g_info, 0, sizeof(g_info));
    memset(g_error, 0, sizeof(g_error));
    memset(g_lobby_addr, 0, sizeof(g_lobby_addr));
    g_lobby_port = 0;
    printf("[lobby-client] Initialized\n");
}

void lobby_client_shutdown(void)
{
    if (!g_initialized) return;
    if (g_conn_id >= 0 &&
        net_socket_state(&g_net, g_conn_id) == NET_CONN_CONNECTED) {
        NetMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = NET_MSG_DISCONNECT;
        msg.disconnect.reason = NET_DISCONNECT_NORMAL;
        net_socket_send_msg(&g_net, g_conn_id, &msg);
    }
    net_socket_shutdown(&g_net);
    g_initialized = false;
    g_state = LOBBY_DISCONNECTED;
    g_conn_id = -1;
    printf("[lobby-client] Shutdown\n");
}

void lobby_client_connect(const char *ip, uint16_t port)
{
    if (!g_initialized) return;

    /* Store for reconnect */
    strncpy(g_lobby_addr, ip, NET_ADDR_LEN - 1);
    g_lobby_port = port;

    g_conn_id = net_socket_connect(&g_net, ip, port);
    if (g_conn_id < 0) {
        snprintf(g_error, sizeof(g_error), "Failed to connect to server");
        g_state = LOBBY_ERROR;
        return;
    }
    g_state = LOBBY_CONNECTING;
    printf("[lobby-client] Connecting to %s:%d\n", ip, port);
}

void lobby_client_disconnect(void)
{
    if (!g_initialized) return;
    if (g_conn_id >= 0) {
        net_socket_close(&g_net, g_conn_id);
        g_conn_id = -1;
    }
    g_state = LOBBY_DISCONNECTED;
}

void lobby_client_update(float dt, const Identity *id)
{
    (void)dt;
    if (!g_initialized || g_conn_id < 0) return;

    net_socket_update(&g_net);

    NetConnState cs = net_socket_state(&g_net, g_conn_id);

    /* Check for connection established */
    if (g_state == LOBBY_CONNECTING && cs == NET_CONN_CONNECTED) {
        g_state = LOBBY_CONNECTED;
        printf("[lobby-client] Connected to lobby\n");
    }

    /* Check for connection lost */
    if (cs == NET_CONN_DISCONNECTED && g_state != LOBBY_DISCONNECTED &&
        g_state != LOBBY_ERROR) {
        snprintf(g_error, sizeof(g_error), "Connection lost");
        g_state = LOBBY_ERROR;
        printf("[lobby-client] Connection lost\n");
        return;
    }

    /* Process incoming messages */
    if (cs == NET_CONN_CONNECTED) {
        NetMsg msg;
        while (net_socket_recv_msg(&g_net, g_conn_id, &msg)) {
            lc_handle_message(&msg, id);
        }
    }

}

void lobby_client_register(const char *username, const Identity *id)
{
    if (g_state != LOBBY_CONNECTED) return;

    strncpy(g_login_username, username, NET_MAX_NAME_LEN - 1);
    g_login_username[NET_MAX_NAME_LEN - 1] = '\0';

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_REGISTER;
    strncpy(msg.reg.username, username, NET_MAX_NAME_LEN - 1);
    memcpy(msg.reg.public_key, id->public_key, IDENTITY_PK_LEN);
    net_socket_send_msg(&g_net, g_conn_id, &msg);

    g_state = LOBBY_REGISTERING;
    printf("[lobby-client] Registering as '%s'\n", username);
}

void lobby_client_login(const char *username)
{
    if (g_state != LOBBY_CONNECTED && g_state != LOBBY_REGISTERING) return;

    strncpy(g_login_username, username, NET_MAX_NAME_LEN - 1);
    g_login_username[NET_MAX_NAME_LEN - 1] = '\0';

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_LOGIN;
    strncpy(msg.login.username, username, NET_MAX_NAME_LEN - 1);
    net_socket_send_msg(&g_net, g_conn_id, &msg);

    g_state = LOBBY_LOGGING_IN;
    printf("[lobby-client] Logging in as '%s'\n", username);
}

void lobby_client_login_by_key(const Identity *id)
{
    if (g_state != LOBBY_CONNECTED) return;

    g_login_username[0] = '\0'; /* no username yet — server will provide it */

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_LOGIN_BY_KEY;
    memcpy(msg.login_by_key.public_key, id->public_key, NET_ED25519_PK_LEN);
    net_socket_send_msg(&g_net, g_conn_id, &msg);

    g_state = LOBBY_LOGGING_IN;
    printf("[lobby-client] Logging in by public key\n");
}

void lobby_client_change_username(const char *new_username)
{
    if (g_state != LOBBY_AUTHENTICATED) return;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_CHANGE_USERNAME;
    memcpy(msg.change_username.auth_token, g_info.auth_token,
           NET_AUTH_TOKEN_LEN);
    strncpy(msg.change_username.new_username, new_username,
            NET_MAX_NAME_LEN - 1);
    net_socket_send_msg(&g_net, g_conn_id, &msg);

    printf("[lobby-client] Requesting username change to '%s'\n",
           new_username);
}

void lobby_client_create_room(void)
{
    if (g_state != LOBBY_AUTHENTICATED) return;
    g_error[0] = '\0';

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_CREATE_ROOM;
    memcpy(msg.create_room.auth_token, g_info.auth_token, NET_AUTH_TOKEN_LEN);
    net_socket_send_msg(&g_net, g_conn_id, &msg);

    g_state = LOBBY_CREATING_ROOM;
    printf("[lobby-client] Creating room\n");
}

void lobby_client_join_room(const char *code)
{
    if (g_state != LOBBY_AUTHENTICATED) return;
    g_error[0] = '\0';

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_JOIN_ROOM;
    memcpy(msg.join_room.auth_token, g_info.auth_token, NET_AUTH_TOKEN_LEN);
    strncpy(msg.join_room.room_code, code, NET_ROOM_CODE_LEN - 1);
    net_socket_send_msg(&g_net, g_conn_id, &msg);

    g_state = LOBBY_JOINING_ROOM;
    printf("[lobby-client] Joining room '%s'\n", code);
}

void lobby_client_queue_matchmake(void)
{
    if (g_state != LOBBY_AUTHENTICATED) return;
    g_error[0] = '\0';

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_QUEUE_MATCHMAKE;
    memcpy(msg.queue_matchmake.auth_token, g_info.auth_token,
           NET_AUTH_TOKEN_LEN);
    net_socket_send_msg(&g_net, g_conn_id, &msg);

    g_state = LOBBY_QUEUED;
    printf("[lobby-client] Entering matchmaking queue\n");
}

void lobby_client_queue_cancel(void)
{
    if (g_state != LOBBY_QUEUED) return;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_QUEUE_CANCEL;
    net_socket_send_msg(&g_net, g_conn_id, &msg);

    g_state = LOBBY_AUTHENTICATED;
    printf("[lobby-client] Cancelled matchmaking\n");
}

void lobby_client_cancel_create(void)
{
    if (g_state != LOBBY_CREATING_ROOM) return;
    g_room_assigned = false;
    g_state = LOBBY_AUTHENTICATED;
    printf("[lobby-client] Cancelled room creation\n");
}

void lobby_client_cancel_join(void)
{
    if (g_state != LOBBY_JOINING_ROOM) return;
    g_room_assigned = false;
    g_state = LOBBY_AUTHENTICATED;
    printf("[lobby-client] Cancelled room join\n");
}

LobbyClientState lobby_client_state(void)
{
    return g_state;
}

const LobbyClientInfo *lobby_client_info(void)
{
    return (g_state == LOBBY_AUTHENTICATED) ? &g_info : NULL;
}

const char *lobby_client_error_msg(void)
{
    return g_error;
}

void lobby_client_clear_error(void)
{
    g_error[0] = '\0';
}

bool lobby_client_has_room_assignment(void)
{
    return g_room_assigned;
}

void lobby_client_consume_room_assignment(char *addr_out, uint16_t *port_out,
                                          char *room_code_out,
                                          uint8_t *token_out)
{
    if (!g_room_assigned) return;
    strncpy(addr_out, g_assigned_addr, NET_ADDR_LEN - 1);
    addr_out[NET_ADDR_LEN - 1] = '\0';
    *port_out = g_assigned_port;
    strncpy(room_code_out, g_assigned_room_code, NET_ROOM_CODE_LEN - 1);
    room_code_out[NET_ROOM_CODE_LEN - 1] = '\0';
    memcpy(token_out, g_assigned_token, NET_AUTH_TOKEN_LEN);
    g_room_assigned = false;
}

void lobby_client_request_stats(void)
{
    if (g_state != LOBBY_AUTHENTICATED || g_conn_id < 0) return;
    g_stats_ready = false;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_STATS_REQUEST;
    memcpy(msg.stats_request.auth_token, g_info.auth_token, NET_AUTH_TOKEN_LEN);
    net_socket_send_msg(&g_net, g_conn_id, &msg);
}

void lobby_client_request_leaderboard(void)
{
    if (g_state != LOBBY_AUTHENTICATED || g_conn_id < 0) return;
    g_leaderboard_ready = false;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_LEADERBOARD_REQUEST;
    memcpy(msg.leaderboard_request.auth_token, g_info.auth_token,
           NET_AUTH_TOKEN_LEN);
    net_socket_send_msg(&g_net, g_conn_id, &msg);
}

bool lobby_client_has_stats(PlayerFullStats *out)
{
    if (!g_stats_ready) return false;
    *out = g_full_stats;
    g_stats_ready = false;
    return true;
}

bool lobby_client_has_leaderboard(LeaderboardData *out)
{
    if (!g_leaderboard_ready) return false;
    *out = g_leaderboard;
    g_leaderboard_ready = false;
    return true;
}

/* ================================================================
 * Friend System — Send Functions
 * ================================================================ */

void lobby_client_friend_search(const char *query)
{
    if (g_state != LOBBY_AUTHENTICATED || g_conn_id < 0) return;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_FRIEND_SEARCH;
    memcpy(msg.friend_search.auth_token, g_info.auth_token, NET_AUTH_TOKEN_LEN);
    strncpy(msg.friend_search.query, query, sizeof(msg.friend_search.query) - 1);
    net_socket_send_msg(&g_net, g_conn_id, &msg);
}

void lobby_client_friend_request(int32_t account_id)
{
    if (g_state != LOBBY_AUTHENTICATED || g_conn_id < 0) return;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_FRIEND_REQUEST;
    memcpy(msg.friend_request.auth_token, g_info.auth_token, NET_AUTH_TOKEN_LEN);
    msg.friend_request.target_account_id = account_id;
    net_socket_send_msg(&g_net, g_conn_id, &msg);
}

void lobby_client_friend_accept(int32_t from_account_id)
{
    if (g_state != LOBBY_AUTHENTICATED || g_conn_id < 0) return;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_FRIEND_ACCEPT;
    memcpy(msg.friend_accept.auth_token, g_info.auth_token, NET_AUTH_TOKEN_LEN);
    msg.friend_accept.from_account_id = from_account_id;
    net_socket_send_msg(&g_net, g_conn_id, &msg);
}

void lobby_client_friend_reject(int32_t from_account_id)
{
    if (g_state != LOBBY_AUTHENTICATED || g_conn_id < 0) return;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_FRIEND_REJECT;
    memcpy(msg.friend_reject.auth_token, g_info.auth_token, NET_AUTH_TOKEN_LEN);
    msg.friend_reject.from_account_id = from_account_id;
    net_socket_send_msg(&g_net, g_conn_id, &msg);
}

void lobby_client_friend_remove(int32_t account_id)
{
    if (g_state != LOBBY_AUTHENTICATED || g_conn_id < 0) return;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_FRIEND_REMOVE;
    memcpy(msg.friend_remove.auth_token, g_info.auth_token, NET_AUTH_TOKEN_LEN);
    msg.friend_remove.target_account_id = account_id;
    net_socket_send_msg(&g_net, g_conn_id, &msg);
}

void lobby_client_friend_list_request(void)
{
    if (g_state != LOBBY_AUTHENTICATED || g_conn_id < 0) return;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_FRIEND_LIST_REQUEST;
    memcpy(msg.friend_list_request.auth_token, g_info.auth_token,
           NET_AUTH_TOKEN_LEN);
    net_socket_send_msg(&g_net, g_conn_id, &msg);
}

void lobby_client_room_invite(int32_t account_id, const char *room_code)
{
    if (g_state != LOBBY_AUTHENTICATED || g_conn_id < 0) return;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_ROOM_INVITE;
    memcpy(msg.room_invite.auth_token, g_info.auth_token, NET_AUTH_TOKEN_LEN);
    msg.room_invite.target_account_id = account_id;
    strncpy(msg.room_invite.room_code, room_code,
            sizeof(msg.room_invite.room_code) - 1);
    net_socket_send_msg(&g_net, g_conn_id, &msg);
}

/* ================================================================
 * Friend System — Polling Functions
 * ================================================================ */

bool lobby_client_has_friend_list(NetMsgFriendList *out)
{
    if (!g_has_friend_list) return false;
    *out = g_friend_list;
    g_has_friend_list = false;
    return true;
}

bool lobby_client_has_friend_search_result(NetMsgFriendSearchResult *out)
{
    if (!g_has_search_result) return false;
    *out = g_search_result;
    g_has_search_result = false;
    return true;
}

bool lobby_client_has_friend_update(NetMsgFriendUpdate *out)
{
    if (g_friend_update_count <= 0) return false;
    *out = g_friend_updates[0];
    for (int i = 1; i < g_friend_update_count; i++)
        g_friend_updates[i - 1] = g_friend_updates[i];
    g_friend_update_count--;
    return true;
}

bool lobby_client_has_friend_request_notify(NetMsgFriendRequestNotify *out)
{
    if (g_friend_req_notif_count <= 0) return false;
    *out = g_friend_req_notifs[0];
    for (int i = 1; i < g_friend_req_notif_count; i++)
        g_friend_req_notifs[i - 1] = g_friend_req_notifs[i];
    g_friend_req_notif_count--;
    return true;
}

bool lobby_client_has_room_invite_notify(NetMsgRoomInviteNotify *out)
{
    if (!g_has_room_invite) return false;
    *out = g_room_invite;
    g_has_room_invite = false;
    return true;
}

bool lobby_client_has_room_invite_expired(NetMsgRoomInviteExpired *out)
{
    if (!g_has_room_invite_expired) return false;
    *out = g_room_invite_expired;
    g_has_room_invite_expired = false;
    return true;
}
