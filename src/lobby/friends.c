/* ============================================================
 * @deps-implements: lobby/friends.h
 * @deps-requires: lobby/friends.h, lobby/db.h, net/protocol.h, net/socket.h,
 *                 sqlite3.h, stdio.h, string.h, time.h
 * @deps-last-changed: 2026-04-06 — Task 4: Lobby Friends Module
 * ============================================================ */

#include "friends.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ================================================================
 * Forward declaration — provided by lobby_net.c (Task 5)
 * ================================================================ */

extern int lobby_net_find_conn_by_account(int32_t account_id);

/* ================================================================
 * Constants
 * ================================================================ */

#define FRIENDS_MAX_PENDING_INVITES  64
#define FRIENDS_MAX_RATE_ENTRIES    128
#define FRIENDS_MAX_FRIENDS          20
#define FRIENDS_MAX_REQUESTS         10
#define FRIENDS_SEARCH_RATE_SECS     1.0
#define FRIENDS_REMOVED_PRESENCE     0xFF

/* ================================================================
 * File-scope state
 * ================================================================ */

static NetSocket *g_net = NULL;

/* In-game presence map */
typedef struct {
    int32_t account_id;
    char    room_code[NET_ROOM_CODE_LEN];
} InGameEntry;

static InGameEntry g_ingame[FRIENDS_MAX_INGAME];
static int         g_ingame_count = 0;

/* Pending room invites */
typedef struct {
    bool    active;
    int32_t inviter;
    int32_t invitee;
    char    room_code[NET_ROOM_CODE_LEN];
} PendingInvite;

static PendingInvite g_invites[FRIENDS_MAX_PENDING_INVITES];

/* Search rate-limit tracking */
typedef struct {
    int    conn_id;
    double last_search;
} RateEntry;

static RateEntry g_rate[FRIENDS_MAX_RATE_ENTRIES];
static int       g_rate_count = 0;

/* ================================================================
 * Internal helpers
 * ================================================================ */

static double time_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void send_msg(int conn_id, NetMsg *msg)
{
    if (g_net) {
        net_socket_send_msg(g_net, conn_id, msg);
    }
}

/* Returns true if the conn is allowed to search (>1s since last search). */
static bool check_search_rate(int conn_id)
{
    double now = time_now();
    for (int i = 0; i < g_rate_count; i++) {
        if (g_rate[i].conn_id == conn_id) {
            if (now - g_rate[i].last_search < FRIENDS_SEARCH_RATE_SECS) {
                return false;
            }
            g_rate[i].last_search = now;
            return true;
        }
    }
    /* New entry */
    if (g_rate_count < FRIENDS_MAX_RATE_ENTRIES) {
        g_rate[g_rate_count].conn_id     = conn_id;
        g_rate[g_rate_count].last_search = now;
        g_rate_count++;
    }
    return true;
}

/* Get username for account_id into out (NET_MAX_NAME_LEN). Returns false if not found. */
static bool get_username(LobbyDB *db, int32_t account_id, char *out)
{
    sqlite3_stmt *stmt = lobbydb_stmt(db, LOBBY_STMT_GET_USERNAME);
    if (!stmt) return false;
    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, account_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        if (name) {
            snprintf(out, NET_MAX_NAME_LEN, "%s", name);
            return true;
        }
    }
    return false;
}

/* Populate out[] with friend account IDs for account_id. Returns count. */
static int get_friend_ids(LobbyDB *db, int32_t account_id, int32_t *out, int max)
{
    sqlite3_stmt *stmt = lobbydb_stmt(db, LOBBY_STMT_FRIEND_LIST);
    if (!stmt) return 0;
    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, account_id);
    int count = 0;
    while (count < max && sqlite3_step(stmt) == SQLITE_ROW) {
        out[count++] = (int32_t)sqlite3_column_int(stmt, 0);
    }
    return count;
}

/* Return current friend count for account_id. */
static int count_friends(LobbyDB *db, int32_t account_id)
{
    sqlite3_stmt *stmt = lobbydb_stmt(db, LOBBY_STMT_FRIEND_COUNT);
    if (!stmt) return 0;
    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, account_id);
    sqlite3_bind_int(stmt, 2, account_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        return sqlite3_column_int(stmt, 0);
    }
    return 0;
}

/* ================================================================
 * Public API — Init
 * ================================================================ */

void friends_init(NetSocket *net)
{
    g_net         = net;
    g_ingame_count = 0;
    g_rate_count  = 0;
    memset(g_ingame,  0, sizeof(g_ingame));
    memset(g_invites, 0, sizeof(g_invites));
    memset(g_rate,    0, sizeof(g_rate));
}

/* ================================================================
 * Presence
 * ================================================================ */

uint8_t friends_get_presence(int32_t account_id)
{
    /* Check in-game map first */
    for (int i = 0; i < g_ingame_count; i++) {
        if (g_ingame[i].account_id == account_id) {
            return (uint8_t)FRIEND_PRESENCE_IN_GAME;
        }
    }
    /* Check if connected to lobby */
    if (lobby_net_find_conn_by_account(account_id) >= 0) {
        return (uint8_t)FRIEND_PRESENCE_ONLINE;
    }
    return (uint8_t)FRIEND_PRESENCE_OFFLINE;
}

/* Send a FRIEND_UPDATE to a single connection. */
static void push_friend_update(int conn_id, int32_t account_id, uint8_t presence)
{
    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type                    = NET_MSG_FRIEND_UPDATE;
    msg.friend_update.account_id = account_id;
    msg.friend_update.presence  = presence;
    send_msg(conn_id, &msg);
}

/* Notify all online friends of account_id with presence value. */
static void notify_friends_presence(int32_t account_id, uint8_t presence, LobbyDB *db)
{
    int32_t friend_ids[FRIENDS_MAX_FRIENDS];
    int     count = get_friend_ids(db, account_id, friend_ids, FRIENDS_MAX_FRIENDS);
    for (int i = 0; i < count; i++) {
        int fconn = lobby_net_find_conn_by_account(friend_ids[i]);
        if (fconn >= 0) {
            push_friend_update(fconn, account_id, presence);
        }
    }
}

/* ================================================================
 * Presence Lifecycle Hooks
 * ================================================================ */

void friends_on_player_authenticated(int conn_id, int32_t account_id, LobbyDB *db)
{
    /* Send this player their full friend list */
    friends_handle_list_request(conn_id, account_id, db);
    /* Notify online friends that this player is now online */
    notify_friends_presence(account_id, (uint8_t)FRIEND_PRESENCE_ONLINE, db);
}

void friends_on_player_disconnected(int32_t account_id, LobbyDB *db)
{
    /* Determine their new presence (may still be in-game) */
    uint8_t presence = friends_get_presence(account_id);
    notify_friends_presence(account_id, presence, db);
}

void friends_on_room_created(const char *room_code, const int32_t *account_ids, int count, LobbyDB *db)
{
    for (int i = 0; i < count; i++) {
        if (g_ingame_count < FRIENDS_MAX_INGAME) {
            g_ingame[g_ingame_count].account_id = account_ids[i];
            snprintf(g_ingame[g_ingame_count].room_code,
                     NET_ROOM_CODE_LEN, "%s", room_code);
            g_ingame_count++;
        }
        notify_friends_presence(account_ids[i], (uint8_t)FRIEND_PRESENCE_IN_GAME, db);
    }
}

void friends_on_room_destroyed(const char *room_code, LobbyDB *db)
{
    /* Collect affected account IDs, then swap-remove them */
    int32_t affected[FRIENDS_MAX_INGAME];
    int     affected_count = 0;

    int i = 0;
    while (i < g_ingame_count) {
        if (strncmp(g_ingame[i].room_code, room_code, NET_ROOM_CODE_LEN) == 0) {
            affected[affected_count++] = g_ingame[i].account_id;
            /* Swap-remove */
            g_ingame[i] = g_ingame[--g_ingame_count];
        } else {
            i++;
        }
    }

    /* Notify friends with new presence (online or offline) */
    for (int j = 0; j < affected_count; j++) {
        uint8_t presence = friends_get_presence(affected[j]);
        notify_friends_presence(affected[j], presence, db);
    }

    /* Expire any room invites for this room */
    friends_expire_room_invites(room_code);
}

/* ================================================================
 * Room Invite Expiration
 * ================================================================ */

void friends_expire_room_invites(const char *room_code)
{
    for (int i = 0; i < FRIENDS_MAX_PENDING_INVITES; i++) {
        if (!g_invites[i].active) continue;
        if (strncmp(g_invites[i].room_code, room_code, NET_ROOM_CODE_LEN) != 0) continue;

        /* Notify invitee that invite expired */
        int conn = lobby_net_find_conn_by_account(g_invites[i].invitee);
        if (conn >= 0) {
            NetMsg msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = NET_MSG_ROOM_INVITE_EXPIRED;
            snprintf(msg.room_invite_expired.room_code,
                     NET_ROOM_CODE_LEN, "%s", room_code);
            send_msg(conn, &msg);
        }

        g_invites[i].active = false;
    }
}

/* ================================================================
 * Message Handlers
 * ================================================================ */

void friends_handle_search(int conn_id, int32_t account_id,
                            const NetMsgFriendSearch *msg, LobbyDB *db)
{
    /* Rate limit */
    if (!check_search_rate(conn_id)) return;

    /* Validate query length — minimum 4 chars */
    /* query is a fixed char[32]; treat as null-terminated */
    size_t qlen = strlen(msg->query);
    if (qlen < 4) return;

    /* Build LIKE pattern "query%" */
    char pattern[sizeof(msg->query) + 2];
    snprintf(pattern, sizeof(pattern), "%s%%", msg->query);

    sqlite3_stmt *stmt = lobbydb_stmt(db, LOBBY_STMT_FRIEND_SEARCH);
    if (!stmt) return;
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, account_id);

    NetMsg out;
    memset(&out, 0, sizeof(out));
    out.type = NET_MSG_FRIEND_SEARCH_RESULT;
    int rcount = 0;

    while (rcount < 10 && sqlite3_step(stmt) == SQLITE_ROW) {
        int32_t     rid      = (int32_t)sqlite3_column_int(stmt, 0);
        const char *rusername = (const char *)sqlite3_column_text(stmt, 1);

        /* Determine status */
        uint8_t status = (uint8_t)FRIEND_STATUS_AVAILABLE;

        /* Check already friends */
        sqlite3_stmt *cs = lobbydb_stmt(db, LOBBY_STMT_FRIEND_CHECK);
        sqlite3_reset(cs);
        sqlite3_bind_int(cs, 1, account_id);
        sqlite3_bind_int(cs, 2, rid);
        if (sqlite3_step(cs) == SQLITE_ROW) {
            status = (uint8_t)FRIEND_STATUS_ALREADY_FRIEND;
            goto store_result;
        }

        /* Check pending sent (account -> rid) */
        cs = lobbydb_stmt(db, LOBBY_STMT_FREQ_CHECK);
        sqlite3_reset(cs);
        sqlite3_bind_int(cs, 1, account_id);
        sqlite3_bind_int(cs, 2, rid);
        if (sqlite3_step(cs) == SQLITE_ROW) {
            status = (uint8_t)FRIEND_STATUS_PENDING_SENT;
            goto store_result;
        }

        /* Check pending received (rid -> account) */
        cs = lobbydb_stmt(db, LOBBY_STMT_FREQ_CHECK);
        sqlite3_reset(cs);
        sqlite3_bind_int(cs, 1, rid);
        sqlite3_bind_int(cs, 2, account_id);
        if (sqlite3_step(cs) == SQLITE_ROW) {
            status = (uint8_t)FRIEND_STATUS_PENDING_RECEIVED;
            goto store_result;
        }

        /* Check blocked (rid blocked account) */
        cs = lobbydb_stmt(db, LOBBY_STMT_FBLOCK_CHECK);
        sqlite3_reset(cs);
        sqlite3_bind_int(cs, 1, rid);
        sqlite3_bind_int(cs, 2, account_id);
        if (sqlite3_step(cs) == SQLITE_ROW) {
            status = (uint8_t)FRIEND_STATUS_BLOCKED;
            goto store_result;
        }

store_result:
        {
            NetFriendSearchEntry *e = &out.friend_search_result.results[rcount];
            e->account_id = rid;
            e->status     = status;
            if (rusername) {
                snprintf(e->username, NET_MAX_NAME_LEN, "%s", rusername);
            }
            rcount++;
        }
    }

    out.friend_search_result.count = (uint8_t)rcount;
    send_msg(conn_id, &out);
}

void friends_handle_request(int conn_id, int32_t account_id,
                             const NetMsgFriendRequest *msg, LobbyDB *db)
{
    int32_t target = msg->target_account_id;

    /* Not self */
    if (target == account_id) return;

    /* Not already friends */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FRIEND_CHECK);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, account_id);
        sqlite3_bind_int(s, 2, target);
        if (sqlite3_step(s) == SQLITE_ROW) return;
    }

    /* Check if target already sent a request to us (auto-accept) */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FREQ_CHECK);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, target);
        sqlite3_bind_int(s, 2, account_id);
        if (sqlite3_step(s) == SQLITE_ROW) {
            /* Mutual request — auto-accept */
            NetMsgFriendAccept fake;
            memset(&fake, 0, sizeof(fake));
            fake.from_account_id = target;
            friends_handle_accept(conn_id, account_id, &fake, db);
            return;
        }
    }

    /* Check blocked by target */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FBLOCK_CHECK);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, target);
        sqlite3_bind_int(s, 2, account_id);
        if (sqlite3_step(s) == SQLITE_ROW) return;
    }

    /* Sender <10 outgoing pending */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FREQ_COUNT_OUTGOING);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, account_id);
        if (sqlite3_step(s) == SQLITE_ROW) {
            if (sqlite3_column_int(s, 0) >= 10) return;
        }
    }

    /* Target <20 friends */
    if (count_friends(db, target) >= FRIENDS_MAX_FRIENDS) return;

    /* Insert request */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FREQ_INSERT);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, account_id);
        sqlite3_bind_int(s, 2, target);
        sqlite3_step(s);
    }

    /* Notify target if online */
    int tconn = lobby_net_find_conn_by_account(target);
    if (tconn >= 0) {
        char uname[NET_MAX_NAME_LEN] = {0};
        get_username(db, account_id, uname);

        NetMsg notify;
        memset(&notify, 0, sizeof(notify));
        notify.type = NET_MSG_FRIEND_REQUEST_NOTIFY;
        notify.friend_request_notify.account_id = account_id;
        snprintf(notify.friend_request_notify.username,
                 NET_MAX_NAME_LEN, "%s", uname);
        send_msg(tconn, &notify);
    }
}

void friends_handle_accept(int conn_id, int32_t account_id,
                            const NetMsgFriendAccept *msg, LobbyDB *db)
{
    int32_t from_id = msg->from_account_id;

    /* Verify request exists: from=from_id, to=account_id */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FREQ_CHECK);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, from_id);
        sqlite3_bind_int(s, 2, account_id);
        if (sqlite3_step(s) != SQLITE_ROW) return;
    }

    /* Check both players <20 friends */
    if (count_friends(db, account_id) >= FRIENDS_MAX_FRIENDS) return;
    if (count_friends(db, from_id)    >= FRIENDS_MAX_FRIENDS) return;

    /* Delete the request */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FREQ_DELETE);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, from_id);
        sqlite3_bind_int(s, 2, account_id);
        sqlite3_step(s);
    }

    /* Insert friendship with canonical order (min, max) */
    {
        int32_t a = (account_id < from_id) ? account_id : from_id;
        int32_t b = (account_id < from_id) ? from_id    : account_id;
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FRIEND_INSERT);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, a);
        sqlite3_bind_int(s, 2, b);
        sqlite3_step(s);
    }

    /* Notify original sender if online */
    int from_conn = lobby_net_find_conn_by_account(from_id);
    if (from_conn >= 0) {
        uint8_t accepter_presence = friends_get_presence(account_id);
        push_friend_update(from_conn, account_id, accepter_presence);
    }

    /* Send FRIEND_UPDATE back to accepter with new friend's presence */
    uint8_t sender_presence = friends_get_presence(from_id);
    push_friend_update(conn_id, from_id, sender_presence);
}

void friends_handle_reject(int conn_id, int32_t account_id,
                            const NetMsgFriendReject *msg, LobbyDB *db)
{
    (void)conn_id;
    int32_t from_id = msg->from_account_id;

    /* Verify request exists */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FREQ_CHECK);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, from_id);
        sqlite3_bind_int(s, 2, account_id);
        if (sqlite3_step(s) != SQLITE_ROW) return;
    }

    /* Delete request */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FREQ_DELETE);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, from_id);
        sqlite3_bind_int(s, 2, account_id);
        sqlite3_step(s);
    }

    /* Insert block: rejecter=account_id blocks requester=from_id */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FBLOCK_INSERT);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, account_id);
        sqlite3_bind_int(s, 2, from_id);
        sqlite3_step(s);
    }
}

void friends_handle_remove(int conn_id, int32_t account_id,
                            const NetMsgFriendRemove *msg, LobbyDB *db)
{
    (void)conn_id;
    int32_t target = msg->target_account_id;

    /* Delete friendship (handles both orderings via 4 bind params) */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FRIEND_DELETE);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, account_id);
        sqlite3_bind_int(s, 2, target);
        sqlite3_bind_int(s, 3, target);
        sqlite3_bind_int(s, 4, account_id);
        sqlite3_step(s);
    }

    /* Notify target with 0xFF = removed */
    int tconn = lobby_net_find_conn_by_account(target);
    if (tconn >= 0) {
        push_friend_update(tconn, account_id, FRIENDS_REMOVED_PRESENCE);
    }

    /* Clean up any pending invites between this pair */
    for (int i = 0; i < FRIENDS_MAX_PENDING_INVITES; i++) {
        if (!g_invites[i].active) continue;
        if ((g_invites[i].inviter == account_id && g_invites[i].invitee == target) ||
            (g_invites[i].inviter == target     && g_invites[i].invitee == account_id)) {
            g_invites[i].active = false;
        }
    }
}

void friends_handle_list_request(int conn_id, int32_t account_id, LobbyDB *db)
{
    NetMsg out;
    memset(&out, 0, sizeof(out));
    out.type = NET_MSG_FRIEND_LIST;

    /* Populate friends */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FRIEND_LIST);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, account_id);
        int fc = 0;
        while (fc < FRIENDS_MAX_FRIENDS && sqlite3_step(s) == SQLITE_ROW) {
            int32_t fid = (int32_t)sqlite3_column_int(s, 0);
            NetFriendEntry *e = &out.friend_list.friends[fc];
            e->account_id = fid;
            e->presence   = friends_get_presence(fid);
            get_username(db, fid, e->username);
            fc++;
        }
        out.friend_list.friend_count = (uint8_t)fc;
    }

    /* Populate incoming requests */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FREQ_LIST_INCOMING);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, account_id);
        int rc = 0;
        while (rc < FRIENDS_MAX_REQUESTS && sqlite3_step(s) == SQLITE_ROW) {
            int32_t     rid  = (int32_t)sqlite3_column_int(s, 0);
            const char *name = (const char *)sqlite3_column_text(s, 1);
            NetFriendRequestEntry *e = &out.friend_list.incoming_requests[rc];
            e->account_id = rid;
            if (name) snprintf(e->username, NET_MAX_NAME_LEN, "%s", name);
            rc++;
        }
        out.friend_list.request_count = (uint8_t)rc;
    }

    send_msg(conn_id, &out);
}

void friends_handle_room_invite(int conn_id, int32_t account_id,
                                 const NetMsgRoomInvite *msg, LobbyDB *db)
{
    (void)conn_id;
    int32_t target = msg->target_account_id;

    /* Verify friendship */
    {
        sqlite3_stmt *s = lobbydb_stmt(db, LOBBY_STMT_FRIEND_CHECK);
        sqlite3_reset(s);
        sqlite3_bind_int(s, 1, account_id);
        sqlite3_bind_int(s, 2, target);
        if (sqlite3_step(s) != SQLITE_ROW) return;
    }

    /* Verify target is online (green dot) */
    if (friends_get_presence(target) != FRIEND_PRESENCE_ONLINE) return;

    /* Find target connection */
    int tconn = lobby_net_find_conn_by_account(target);
    if (tconn < 0) return;

    /* Store in pending invites */
    for (int i = 0; i < FRIENDS_MAX_PENDING_INVITES; i++) {
        if (!g_invites[i].active) {
            g_invites[i].active  = true;
            g_invites[i].inviter = account_id;
            g_invites[i].invitee = target;
            snprintf(g_invites[i].room_code, NET_ROOM_CODE_LEN,
                     "%s", msg->room_code);
            break;
        }
    }

    /* Send ROOM_INVITE_NOTIFY to target */
    char uname[NET_MAX_NAME_LEN] = {0};
    get_username(db, account_id, uname);

    NetMsg notify;
    memset(&notify, 0, sizeof(notify));
    notify.type = NET_MSG_ROOM_INVITE_NOTIFY;
    snprintf(notify.room_invite_notify.from_username,
             NET_MAX_NAME_LEN, "%s", uname);
    snprintf(notify.room_invite_notify.room_code,
             NET_ROOM_CODE_LEN, "%s", msg->room_code);
    send_msg(tconn, &notify);
}
