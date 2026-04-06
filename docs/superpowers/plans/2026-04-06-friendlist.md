# Friendlist System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a complete friend system — search, request, accept/reject, presence, room invites, and removal — to Hollow Hearts.

**Architecture:** Lobby-centric. All friend logic lives in the lobby server with SQLite persistence. The client renders a friend panel on the left side of the online menu. Protocol extended with 13 new message types. Presence derived from lobby connections + room lifecycle events.

**Tech Stack:** C11, SQLite, Raylib (client rendering), custom TCP protocol.

**Spec:** `docs/superpowers/specs/2026-04-06-friendlist-design.md`

---

## File Structure

| Action | File | Responsibility |
|--------|------|---------------|
| Create | `src/lobby/friends.c` | Lobby-side friend DB queries, handler logic, presence tracking |
| Create | `src/lobby/friends.h` | Public API for friend handlers |
| Create | `src/game/friend_panel.c` | Client friend panel UI logic, state, input handling |
| Create | `src/game/friend_panel.h` | FriendPanelState, FriendEntry types, public API |
| Create | `src/render/friend_panel_render.c` | Friend panel drawing, layout, scrolling, hit-testing |
| Create | `src/render/friend_panel_render.h` | Render API for friend panel |
| Modify | `src/lobby/db.h` | New prepared statement IDs for friend queries |
| Modify | `src/lobby/db.c` | Migration v8 (3 tables), new prepared statements |
| Modify | `src/net/protocol.h` | 13 new message types, structs, NetMsg union members |
| Modify | `src/net/protocol.c` | Serialize/deserialize for new message types |
| Modify | `src/lobby/lobby_net.c` | Dispatch new friend message types to handlers, presence hooks in auth/disconnect |
| Modify | `src/net/lobby_client.h` | New friend API functions, FriendPanelState forward decl |
| Modify | `src/net/lobby_client.c` | Send friend messages, receive and apply friend updates |
| Modify | `src/game/online_ui.c` | Integrate friend panel into online menu flow |
| Modify | `src/game/online_ui.h` | Add FriendPanelState to OnlineUIState |
| Modify | `src/render/render.c` | Call friend panel render in online menu draw path |

All new `.c` files are auto-discovered by Makefile wildcards — no Makefile changes needed.

---

### Task 1: Database Migration — Friend Tables

**Files:**
- Modify: `src/lobby/db.h:14-61` (LOBBY_SCHEMA_VERSION, LobbyStmtID enum)
- Modify: `src/lobby/db.c:18,30-118,230-300` (schema version, migrations array, prepared statements)

- [ ] **Step 1: Bump schema version**

In `src/lobby/db.c`, change:
```c
#define LOBBY_SCHEMA_VERSION 8
```

- [ ] **Step 2: Add migration v8 to MIGRATIONS array**

In `src/lobby/db.c`, add after the `{7, ...}` entry in `MIGRATIONS[]`:

```c
    {8,
     "CREATE TABLE friendships ("
     "  account_a  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
     "  account_b  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
     "  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
     "  PRIMARY KEY (account_a, account_b),"
     "  CHECK(account_a < account_b)"
     ");"
     "CREATE INDEX idx_friendships_b ON friendships(account_b);"
     "CREATE TABLE friend_requests ("
     "  from_account INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
     "  to_account   INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
     "  created_at   INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
     "  PRIMARY KEY (from_account, to_account)"
     ");"
     "CREATE INDEX idx_friend_requests_to ON friend_requests(to_account);"
     "CREATE TABLE friend_blocks ("
     "  blocker_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
     "  blocked_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
     "  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
     "  PRIMARY KEY (blocker_id, blocked_id)"
     ");",
     "friendships, friend_requests, friend_blocks tables"},
```

- [ ] **Step 3: Add prepared statement IDs**

In `src/lobby/db.h`, add before `LOBBY_STMT__COUNT`:

```c
    /* Friend system (migration v8) */
    LOBBY_STMT_FRIEND_SEARCH,
    LOBBY_STMT_FRIEND_COUNT,
    LOBBY_STMT_FRIEND_LIST,
    LOBBY_STMT_FRIEND_INSERT,
    LOBBY_STMT_FRIEND_DELETE,
    LOBBY_STMT_FRIEND_CHECK,
    LOBBY_STMT_FREQ_INSERT,
    LOBBY_STMT_FREQ_DELETE,
    LOBBY_STMT_FREQ_LIST_INCOMING,
    LOBBY_STMT_FREQ_LIST_OUTGOING,
    LOBBY_STMT_FREQ_CHECK,
    LOBBY_STMT_FREQ_COUNT_OUTGOING,
    LOBBY_STMT_FBLOCK_INSERT,
    LOBBY_STMT_FBLOCK_CHECK,
```

- [ ] **Step 4: Add prepared statement SQL strings**

In `src/lobby/db.c`, add to the `STMT_SQL[]` array (follows the `[LOBBY_STMT_xxx] = "..."` pattern):

```c
    [LOBBY_STMT_FRIEND_SEARCH] =
        "SELECT id, username FROM accounts "
        "WHERE username LIKE ? COLLATE NOCASE AND id != ? "
        "LIMIT 10",

    [LOBBY_STMT_FRIEND_COUNT] =
        "SELECT COUNT(*) FROM friendships "
        "WHERE account_a = ? OR account_b = ?",

    [LOBBY_STMT_FRIEND_LIST] =
        "SELECT CASE WHEN account_a = ?1 THEN account_b ELSE account_a END AS friend_id "
        "FROM friendships "
        "WHERE account_a = ?1 OR account_b = ?1",

    [LOBBY_STMT_FRIEND_INSERT] =
        "INSERT OR IGNORE INTO friendships (account_a, account_b) VALUES (?, ?)",

    [LOBBY_STMT_FRIEND_DELETE] =
        "DELETE FROM friendships "
        "WHERE (account_a = ? AND account_b = ?) OR (account_a = ? AND account_b = ?)",

    [LOBBY_STMT_FRIEND_CHECK] =
        "SELECT 1 FROM friendships "
        "WHERE (account_a = ?1 AND account_b = ?2) OR (account_a = ?2 AND account_b = ?1) "
        "LIMIT 1",

    [LOBBY_STMT_FREQ_INSERT] =
        "INSERT OR IGNORE INTO friend_requests (from_account, to_account) VALUES (?, ?)",

    [LOBBY_STMT_FREQ_DELETE] =
        "DELETE FROM friend_requests WHERE from_account = ? AND to_account = ?",

    [LOBBY_STMT_FREQ_LIST_INCOMING] =
        "SELECT fr.from_account, a.username FROM friend_requests fr "
        "JOIN accounts a ON fr.from_account = a.id "
        "WHERE fr.to_account = ?",

    [LOBBY_STMT_FREQ_LIST_OUTGOING] =
        "SELECT to_account FROM friend_requests WHERE from_account = ?",

    [LOBBY_STMT_FREQ_CHECK] =
        "SELECT 1 FROM friend_requests "
        "WHERE from_account = ? AND to_account = ? LIMIT 1",

    [LOBBY_STMT_FREQ_COUNT_OUTGOING] =
        "SELECT COUNT(*) FROM friend_requests WHERE from_account = ?",

    [LOBBY_STMT_FBLOCK_INSERT] =
        "INSERT OR IGNORE INTO friend_blocks (blocker_id, blocked_id) VALUES (?, ?)",

    [LOBBY_STMT_FBLOCK_CHECK] =
        "SELECT 1 FROM friend_blocks "
        "WHERE blocker_id = ? AND blocked_id = ? LIMIT 1",
```

- [ ] **Step 5: Build lobby and verify**

Run: `make lobby`
Expected: Clean build, no errors.

- [ ] **Step 6: Commit**

```bash
git add src/lobby/db.h src/lobby/db.c
git commit -m "feat(db): add migration v8 — friendships, friend_requests, friend_blocks tables and prepared statements"
```

---

### Task 2: Protocol Messages — Structs and Enum

**Files:**
- Modify: `src/net/protocol.h:107-118,636-685`

- [ ] **Step 1: Add friend message type IDs**

In `src/net/protocol.h`, add after line 107 (`NET_MSG_LEADERBOARD_RESPONSE = 57`) and before the server messages block:

```c
    /* Friend system messages (70-82) */
    NET_MSG_FRIEND_SEARCH          = 70,  /* client -> lobby: username prefix search */
    NET_MSG_FRIEND_SEARCH_RESULT   = 71,  /* lobby -> client: search results */
    NET_MSG_FRIEND_REQUEST         = 72,  /* client -> lobby: send friend request */
    NET_MSG_FRIEND_ACCEPT          = 73,  /* client -> lobby: accept request */
    NET_MSG_FRIEND_REJECT          = 74,  /* client -> lobby: reject request (blocks) */
    NET_MSG_FRIEND_REMOVE          = 75,  /* client -> lobby: remove friend */
    NET_MSG_FRIEND_LIST_REQUEST    = 76,  /* client -> lobby: request full list */
    NET_MSG_FRIEND_LIST            = 77,  /* lobby -> client: full friend list */
    NET_MSG_FRIEND_UPDATE          = 78,  /* lobby -> client: single friend presence change */
    NET_MSG_FRIEND_REQUEST_NOTIFY  = 79,  /* lobby -> client: incoming request push */
    NET_MSG_ROOM_INVITE            = 80,  /* client -> lobby: invite friend to room */
    NET_MSG_ROOM_INVITE_NOTIFY     = 81,  /* lobby -> client: room invite push */
    NET_MSG_ROOM_INVITE_EXPIRED    = 82,  /* lobby -> client: invite no longer valid */
```

- [ ] **Step 2: Add friend message structs**

In `src/net/protocol.h`, add after `NetMsgPassConfirmed` (around line 451) and before `NetTrickView`:

```c
/* ================================================================
 * Friend System Messages
 * ================================================================ */

typedef enum FriendSearchStatus {
    FRIEND_STATUS_AVAILABLE       = 0,
    FRIEND_STATUS_ALREADY_FRIEND  = 1,
    FRIEND_STATUS_PENDING_SENT    = 2,
    FRIEND_STATUS_PENDING_RECEIVED = 3,
    FRIEND_STATUS_BLOCKED         = 4,
    FRIEND_STATUS_SELF            = 5,
} FriendSearchStatus;

typedef enum FriendPresence {
    FRIEND_PRESENCE_OFFLINE  = 0,
    FRIEND_PRESENCE_ONLINE   = 1,
    FRIEND_PRESENCE_IN_GAME  = 2,
} FriendPresence;

typedef struct NetMsgFriendSearch {
    uint8_t auth_token[32];
    char    query[32];
} NetMsgFriendSearch;

typedef struct NetFriendSearchEntry {
    char    username[32];
    int32_t account_id;
    uint8_t status;  /* FriendSearchStatus */
} NetFriendSearchEntry;

typedef struct NetMsgFriendSearchResult {
    uint8_t              count;
    NetFriendSearchEntry results[10];
} NetMsgFriendSearchResult;

typedef struct NetMsgFriendRequest {
    uint8_t auth_token[32];
    int32_t target_account_id;
} NetMsgFriendRequest;

typedef struct NetMsgFriendAccept {
    uint8_t auth_token[32];
    int32_t from_account_id;
} NetMsgFriendAccept;

typedef struct NetMsgFriendReject {
    uint8_t auth_token[32];
    int32_t from_account_id;
} NetMsgFriendReject;

typedef struct NetMsgFriendRemove {
    uint8_t auth_token[32];
    int32_t target_account_id;
} NetMsgFriendRemove;

typedef struct NetMsgFriendListRequest {
    uint8_t auth_token[32];
} NetMsgFriendListRequest;

typedef struct NetFriendEntry {
    char    username[32];
    int32_t account_id;
    uint8_t presence;  /* FriendPresence */
} NetFriendEntry;

typedef struct NetFriendRequestEntry {
    char    username[32];
    int32_t account_id;
} NetFriendRequestEntry;

typedef struct NetMsgFriendList {
    uint8_t             friend_count;
    NetFriendEntry      friends[20];
    uint8_t             request_count;
    NetFriendRequestEntry incoming_requests[10];
} NetMsgFriendList;

typedef struct NetMsgFriendUpdate {
    int32_t account_id;
    uint8_t presence;   /* FriendPresence — 0xFF = removed */
} NetMsgFriendUpdate;

typedef struct NetMsgFriendRequestNotify {
    char    username[32];
    int32_t account_id;
} NetMsgFriendRequestNotify;

typedef struct NetMsgRoomInvite {
    uint8_t auth_token[32];
    int32_t target_account_id;
} NetMsgRoomInvite;

typedef struct NetMsgRoomInviteNotify {
    char from_username[32];
    char room_code[8];
} NetMsgRoomInviteNotify;

typedef struct NetMsgRoomInviteExpired {
    char room_code[8];
} NetMsgRoomInviteExpired;
```

- [ ] **Step 3: Add union members to NetMsg**

In `src/net/protocol.h`, add inside the `NetMsg` union (after `start_game`):

```c
        /* Friend system */
        NetMsgFriendSearch        friend_search;
        NetMsgFriendSearchResult  friend_search_result;
        NetMsgFriendRequest       friend_request;
        NetMsgFriendAccept        friend_accept;
        NetMsgFriendReject        friend_reject;
        NetMsgFriendRemove        friend_remove;
        NetMsgFriendListRequest   friend_list_request;
        NetMsgFriendList          friend_list;
        NetMsgFriendUpdate        friend_update;
        NetMsgFriendRequestNotify friend_request_notify;
        NetMsgRoomInvite          room_invite;
        NetMsgRoomInviteNotify    room_invite_notify;
        NetMsgRoomInviteExpired   room_invite_expired;
```

- [ ] **Step 4: Build all and verify**

Run: `make debug-all`
Expected: Clean build, no errors.

- [ ] **Step 5: Commit**

```bash
git add src/net/protocol.h
git commit -m "feat(protocol): add 13 friend system message types and structs"
```

---

### Task 3: Protocol Serialization

**Files:**
- Modify: `src/net/protocol.c` (serialize and deserialize switch blocks)

This task adds serialization and deserialization cases for all 13 friend message types. The existing pattern uses helper functions `ser_*` / `deser_*` with raw byte packing.

- [ ] **Step 1: Add serialize helper functions**

In `src/net/protocol.c`, add serialize helpers before the main `net_msg_serialize` function. Follow the existing pattern (e.g., `ser_leaderboard_response`). Each writes fields into a byte buffer and returns bytes written.

```c
/* ================================================================
 * Friend System Serialization
 * ================================================================ */

static int ser_friend_search(const NetMsgFriendSearch *m, uint8_t *buf, size_t cap)
{
    size_t need = 32 + 32;
    if (cap < need) return -1;
    memcpy(buf, m->auth_token, 32);
    memcpy(buf + 32, m->query, 32);
    return (int)need;
}

static int deser_friend_search(NetMsgFriendSearch *m, const uint8_t *buf, size_t len)
{
    if (len < 64) return -1;
    memcpy(m->auth_token, buf, 32);
    memcpy(m->query, buf + 32, 32);
    m->query[31] = '\0';
    return 64;
}

static int ser_friend_search_result(const NetMsgFriendSearchResult *m, uint8_t *buf, size_t cap)
{
    /* count(1) + count * (username(32) + account_id(4) + status(1)) */
    size_t need = 1 + m->count * 37;
    if (cap < need) return -1;
    buf[0] = m->count;
    size_t off = 1;
    for (int i = 0; i < m->count; i++) {
        memcpy(buf + off, m->results[i].username, 32); off += 32;
        write_i32(buf + off, m->results[i].account_id); off += 4;
        buf[off++] = m->results[i].status;
    }
    return (int)off;
}

static int deser_friend_search_result(NetMsgFriendSearchResult *m, const uint8_t *buf, size_t len)
{
    if (len < 1) return -1;
    m->count = buf[0];
    if (m->count > 10) m->count = 10;
    size_t off = 1;
    for (int i = 0; i < m->count; i++) {
        if (off + 37 > len) return -1;
        memcpy(m->results[i].username, buf + off, 32); off += 32;
        m->results[i].username[31] = '\0';
        m->results[i].account_id = read_i32(buf + off); off += 4;
        m->results[i].status = buf[off++];
    }
    return (int)off;
}

static int ser_friend_request(const NetMsgFriendRequest *m, uint8_t *buf, size_t cap)
{
    if (cap < 36) return -1;
    memcpy(buf, m->auth_token, 32);
    write_i32(buf + 32, m->target_account_id);
    return 36;
}

static int deser_friend_request(NetMsgFriendRequest *m, const uint8_t *buf, size_t len)
{
    if (len < 36) return -1;
    memcpy(m->auth_token, buf, 32);
    m->target_account_id = read_i32(buf + 32);
    return 36;
}

static int ser_friend_accept(const NetMsgFriendAccept *m, uint8_t *buf, size_t cap)
{
    if (cap < 36) return -1;
    memcpy(buf, m->auth_token, 32);
    write_i32(buf + 32, m->from_account_id);
    return 36;
}

static int deser_friend_accept(NetMsgFriendAccept *m, const uint8_t *buf, size_t len)
{
    if (len < 36) return -1;
    memcpy(m->auth_token, buf, 32);
    m->from_account_id = read_i32(buf + 32);
    return 36;
}

static int ser_friend_reject(const NetMsgFriendReject *m, uint8_t *buf, size_t cap)
{
    if (cap < 36) return -1;
    memcpy(buf, m->auth_token, 32);
    write_i32(buf + 32, m->from_account_id);
    return 36;
}

static int deser_friend_reject(NetMsgFriendReject *m, const uint8_t *buf, size_t len)
{
    if (len < 36) return -1;
    memcpy(m->auth_token, buf, 32);
    m->from_account_id = read_i32(buf + 32);
    return 36;
}

static int ser_friend_remove(const NetMsgFriendRemove *m, uint8_t *buf, size_t cap)
{
    if (cap < 36) return -1;
    memcpy(buf, m->auth_token, 32);
    write_i32(buf + 32, m->target_account_id);
    return 36;
}

static int deser_friend_remove(NetMsgFriendRemove *m, const uint8_t *buf, size_t len)
{
    if (len < 36) return -1;
    memcpy(m->auth_token, buf, 32);
    m->target_account_id = read_i32(buf + 32);
    return 36;
}

static int ser_friend_list_request(const NetMsgFriendListRequest *m, uint8_t *buf, size_t cap)
{
    if (cap < 32) return -1;
    memcpy(buf, m->auth_token, 32);
    return 32;
}

static int deser_friend_list_request(NetMsgFriendListRequest *m, const uint8_t *buf, size_t len)
{
    if (len < 32) return -1;
    memcpy(m->auth_token, buf, 32);
    return 32;
}

static int ser_friend_list(const NetMsgFriendList *m, uint8_t *buf, size_t cap)
{
    /* friend_count(1) + friends(count * 37) + request_count(1) + requests(count * 36) */
    size_t need = 1 + (size_t)m->friend_count * 37 + 1 + (size_t)m->request_count * 36;
    if (cap < need) return -1;
    size_t off = 0;
    buf[off++] = m->friend_count;
    for (int i = 0; i < m->friend_count; i++) {
        memcpy(buf + off, m->friends[i].username, 32); off += 32;
        write_i32(buf + off, m->friends[i].account_id); off += 4;
        buf[off++] = m->friends[i].presence;
    }
    buf[off++] = m->request_count;
    for (int i = 0; i < m->request_count; i++) {
        memcpy(buf + off, m->incoming_requests[i].username, 32); off += 32;
        write_i32(buf + off, m->incoming_requests[i].account_id); off += 4;
    }
    return (int)off;
}

static int deser_friend_list(NetMsgFriendList *m, const uint8_t *buf, size_t len)
{
    if (len < 2) return -1;
    size_t off = 0;
    m->friend_count = buf[off++];
    if (m->friend_count > 20) m->friend_count = 20;
    for (int i = 0; i < m->friend_count; i++) {
        if (off + 37 > len) return -1;
        memcpy(m->friends[i].username, buf + off, 32); off += 32;
        m->friends[i].username[31] = '\0';
        m->friends[i].account_id = read_i32(buf + off); off += 4;
        m->friends[i].presence = buf[off++];
    }
    if (off >= len) return -1;
    m->request_count = buf[off++];
    if (m->request_count > 10) m->request_count = 10;
    for (int i = 0; i < m->request_count; i++) {
        if (off + 36 > len) return -1;
        memcpy(m->incoming_requests[i].username, buf + off, 32); off += 32;
        m->incoming_requests[i].username[31] = '\0';
        m->incoming_requests[i].account_id = read_i32(buf + off); off += 4;
    }
    return (int)off;
}

static int ser_friend_update(const NetMsgFriendUpdate *m, uint8_t *buf, size_t cap)
{
    if (cap < 5) return -1;
    write_i32(buf, m->account_id);
    buf[4] = m->presence;
    return 5;
}

static int deser_friend_update(NetMsgFriendUpdate *m, const uint8_t *buf, size_t len)
{
    if (len < 5) return -1;
    m->account_id = read_i32(buf);
    m->presence = buf[4];
    return 5;
}

static int ser_friend_request_notify(const NetMsgFriendRequestNotify *m, uint8_t *buf, size_t cap)
{
    if (cap < 36) return -1;
    memcpy(buf, m->username, 32);
    write_i32(buf + 32, m->account_id);
    return 36;
}

static int deser_friend_request_notify(NetMsgFriendRequestNotify *m, const uint8_t *buf, size_t len)
{
    if (len < 36) return -1;
    memcpy(m->username, buf, 32);
    m->username[31] = '\0';
    m->account_id = read_i32(buf + 32);
    return 36;
}

static int ser_room_invite(const NetMsgRoomInvite *m, uint8_t *buf, size_t cap)
{
    if (cap < 36) return -1;
    memcpy(buf, m->auth_token, 32);
    write_i32(buf + 32, m->target_account_id);
    return 36;
}

static int deser_room_invite(NetMsgRoomInvite *m, const uint8_t *buf, size_t len)
{
    if (len < 36) return -1;
    memcpy(m->auth_token, buf, 32);
    m->target_account_id = read_i32(buf + 32);
    return 36;
}

static int ser_room_invite_notify(const NetMsgRoomInviteNotify *m, uint8_t *buf, size_t cap)
{
    if (cap < 40) return -1;
    memcpy(buf, m->from_username, 32);
    memcpy(buf + 32, m->room_code, 8);
    return 40;
}

static int deser_room_invite_notify(NetMsgRoomInviteNotify *m, const uint8_t *buf, size_t len)
{
    if (len < 40) return -1;
    memcpy(m->from_username, buf, 32);
    m->from_username[31] = '\0';
    memcpy(m->room_code, buf + 32, 8);
    m->room_code[7] = '\0';
    return 40;
}

static int ser_room_invite_expired(const NetMsgRoomInviteExpired *m, uint8_t *buf, size_t cap)
{
    if (cap < 8) return -1;
    memcpy(buf, m->room_code, 8);
    return 8;
}

static int deser_room_invite_expired(NetMsgRoomInviteExpired *m, const uint8_t *buf, size_t len)
{
    if (len < 8) return -1;
    memcpy(m->room_code, buf, 8);
    m->room_code[7] = '\0';
    return 8;
}
```

- [ ] **Step 2: Add serialize switch cases**

In `net_msg_serialize`, add cases after `NET_MSG_PASS_CONFIRMED`:

```c
    case NET_MSG_FRIEND_SEARCH:
        n = ser_friend_search(&msg->friend_search, payload, remaining); break;
    case NET_MSG_FRIEND_SEARCH_RESULT:
        n = ser_friend_search_result(&msg->friend_search_result, payload, remaining); break;
    case NET_MSG_FRIEND_REQUEST:
        n = ser_friend_request(&msg->friend_request, payload, remaining); break;
    case NET_MSG_FRIEND_ACCEPT:
        n = ser_friend_accept(&msg->friend_accept, payload, remaining); break;
    case NET_MSG_FRIEND_REJECT:
        n = ser_friend_reject(&msg->friend_reject, payload, remaining); break;
    case NET_MSG_FRIEND_REMOVE:
        n = ser_friend_remove(&msg->friend_remove, payload, remaining); break;
    case NET_MSG_FRIEND_LIST_REQUEST:
        n = ser_friend_list_request(&msg->friend_list_request, payload, remaining); break;
    case NET_MSG_FRIEND_LIST:
        n = ser_friend_list(&msg->friend_list, payload, remaining); break;
    case NET_MSG_FRIEND_UPDATE:
        n = ser_friend_update(&msg->friend_update, payload, remaining); break;
    case NET_MSG_FRIEND_REQUEST_NOTIFY:
        n = ser_friend_request_notify(&msg->friend_request_notify, payload, remaining); break;
    case NET_MSG_ROOM_INVITE:
        n = ser_room_invite(&msg->room_invite, payload, remaining); break;
    case NET_MSG_ROOM_INVITE_NOTIFY:
        n = ser_room_invite_notify(&msg->room_invite_notify, payload, remaining); break;
    case NET_MSG_ROOM_INVITE_EXPIRED:
        n = ser_room_invite_expired(&msg->room_invite_expired, payload, remaining); break;
```

- [ ] **Step 3: Add deserialize switch cases**

In `net_msg_deserialize`, add matching cases:

```c
    case NET_MSG_FRIEND_SEARCH:
        n = deser_friend_search(&msg->friend_search, payload, remaining); break;
    case NET_MSG_FRIEND_SEARCH_RESULT:
        n = deser_friend_search_result(&msg->friend_search_result, payload, remaining); break;
    case NET_MSG_FRIEND_REQUEST:
        n = deser_friend_request(&msg->friend_request, payload, remaining); break;
    case NET_MSG_FRIEND_ACCEPT:
        n = deser_friend_accept(&msg->friend_accept, payload, remaining); break;
    case NET_MSG_FRIEND_REJECT:
        n = deser_friend_reject(&msg->friend_reject, payload, remaining); break;
    case NET_MSG_FRIEND_REMOVE:
        n = deser_friend_remove(&msg->friend_remove, payload, remaining); break;
    case NET_MSG_FRIEND_LIST_REQUEST:
        n = deser_friend_list_request(&msg->friend_list_request, payload, remaining); break;
    case NET_MSG_FRIEND_LIST:
        n = deser_friend_list(&msg->friend_list, payload, remaining); break;
    case NET_MSG_FRIEND_UPDATE:
        n = deser_friend_update(&msg->friend_update, payload, remaining); break;
    case NET_MSG_FRIEND_REQUEST_NOTIFY:
        n = deser_friend_request_notify(&msg->friend_request_notify, payload, remaining); break;
    case NET_MSG_ROOM_INVITE:
        n = deser_room_invite(&msg->room_invite, payload, remaining); break;
    case NET_MSG_ROOM_INVITE_NOTIFY:
        n = deser_room_invite_notify(&msg->room_invite_notify, payload, remaining); break;
    case NET_MSG_ROOM_INVITE_EXPIRED:
        n = deser_room_invite_expired(&msg->room_invite_expired, payload, remaining); break;
```

- [ ] **Step 4: Build all and verify**

Run: `make debug-all`
Expected: Clean build, no errors.

- [ ] **Step 5: Commit**

```bash
git add src/net/protocol.c
git commit -m "feat(protocol): serialize/deserialize for 13 friend message types"
```

---

### Task 4: Lobby Friends Module — Core Logic

**Files:**
- Create: `src/lobby/friends.h`
- Create: `src/lobby/friends.c`

- [ ] **Step 1: Create friends.h**

```c
#ifndef FRIENDS_H
#define FRIENDS_H

#include "db.h"
#include "../net/protocol.h"
#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * Presence Tracking
 * ================================================================ */

/* Max tracked in-game players (4 per room * reasonable room count) */
#define FRIENDS_MAX_INGAME 256

void friends_init(void);

/* Call when a player authenticates — pushes FRIEND_UPDATE(online) to their online friends,
   sends FRIEND_LIST to the newly authenticated player */
void friends_on_player_authenticated(int conn_id, int32_t account_id, LobbyDB *db);

/* Call when a player disconnects — pushes FRIEND_UPDATE(offline) to their online friends */
void friends_on_player_disconnected(int32_t account_id, LobbyDB *db);

/* Call when SERVER_ROOM_CREATED arrives — marks players as in-game */
void friends_on_room_created(const char *room_code, const int32_t *account_ids, int count, LobbyDB *db);

/* Call when SERVER_ROOM_DESTROYED arrives — clears in-game status */
void friends_on_room_destroyed(const char *room_code, LobbyDB *db);

/* Call from lby_on_dead_server — clears all rooms for that server */
void friends_on_server_dead(int server_conn_id);

/* ================================================================
 * Presence Queries
 * ================================================================ */

/* Returns FriendPresence for an account_id */
uint8_t friends_get_presence(int32_t account_id);

/* ================================================================
 * Message Handlers (called from lobby_net.c dispatch)
 * ================================================================ */

void friends_handle_search(int conn_id, int32_t account_id, const NetMsgFriendSearch *msg, LobbyDB *db);
void friends_handle_request(int conn_id, int32_t account_id, const NetMsgFriendRequest *msg, LobbyDB *db);
void friends_handle_accept(int conn_id, int32_t account_id, const NetMsgFriendAccept *msg, LobbyDB *db);
void friends_handle_reject(int conn_id, int32_t account_id, const NetMsgFriendReject *msg, LobbyDB *db);
void friends_handle_remove(int conn_id, int32_t account_id, const NetMsgFriendRemove *msg, LobbyDB *db);
void friends_handle_list_request(int conn_id, int32_t account_id, const NetMsgFriendListRequest *msg, LobbyDB *db);
void friends_handle_room_invite(int conn_id, int32_t account_id, const NetMsgRoomInvite *msg, LobbyDB *db);

/* ================================================================
 * Room Invite Management
 * ================================================================ */

/* Expire all pending invites for a room (call on room start/fill/destroy) */
void friends_expire_room_invites(const char *room_code);

#endif /* FRIENDS_H */
```

- [ ] **Step 2: Create friends.c — includes, file-scope state, and helpers**

```c
#include "friends.h"
#include "../net/socket.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ================================================================
 * External references (from lobby_net.c)
 * ================================================================ */

extern NetSocket g_net;

/* ================================================================
 * In-game presence map
 * ================================================================ */

typedef struct {
    int32_t account_id;
    char    room_code[8];
} InGameEntry;

static InGameEntry g_ingame[FRIENDS_MAX_INGAME];
static int         g_ingame_count = 0;

/* ================================================================
 * Pending room invites (ephemeral, in-memory)
 * ================================================================ */

#define MAX_PENDING_INVITES 64

typedef struct {
    bool    active;
    int32_t inviter_account_id;
    int32_t invitee_account_id;
    char    room_code[8];
} PendingInvite;

static PendingInvite g_invites[MAX_PENDING_INVITES];

/* ================================================================
 * Rate limiting
 * ================================================================ */

#define MAX_TRACKED_CONNECTIONS 128

static struct {
    int     conn_id;
    double  last_search_time;
} g_rate_limit[MAX_TRACKED_CONNECTIONS];

/* ================================================================
 * Helpers
 * ================================================================ */

/* Find connection ID for an authenticated account_id.
   Returns -1 if not found (offline). */
static int find_conn_for_account(int32_t account_id);

/* Get username for an account_id from DB */
static bool get_username(LobbyDB *db, int32_t account_id, char *out_username);

/* Get friend list (account_ids) for an account */
static int get_friend_ids(LobbyDB *db, int32_t account_id, int32_t *out_ids, int max);

/* Count friends for an account */
static int count_friends(LobbyDB *db, int32_t account_id);

/* Send a NetMsg to a connection */
static void send_msg(int conn_id, NetMsg *msg);

/* Check rate limit — returns true if allowed */
static bool check_search_rate(int conn_id);

void friends_init(void)
{
    memset(g_ingame, 0, sizeof(g_ingame));
    g_ingame_count = 0;
    memset(g_invites, 0, sizeof(g_invites));
    memset(g_rate_limit, 0, sizeof(g_rate_limit));
}
```

The implementer must flesh out:
- `find_conn_for_account` — iterate `g_net.conns`, check `user_data` or a side table mapping account_id to conn_id. **NOTE:** `LobbyConnInfo` is in `lobby_net.c` as file-scope. The implementer will need to either expose a lookup function from `lobby_net.c` or make the `LobbyConnInfo` array accessible. Recommended approach: add a `lobby_net_find_conn_by_account(int32_t account_id)` function to `lobby_net.c/h`.
- `get_username` — use `LOBBY_STMT_FIND_BY_USERNAME` or add a simple `SELECT username FROM accounts WHERE id = ?` stmt.
- `get_friend_ids` — use `LOBBY_STMT_FRIEND_LIST`.
- `count_friends` — use `LOBBY_STMT_FRIEND_COUNT`.
- `send_msg` — call `net_socket_send_msg(&g_net, conn_id, msg)`.
- `check_search_rate` — check timestamp in `g_rate_limit`, return false if <1s since last search.

- [ ] **Step 3: Implement friends_handle_search**

```c
void friends_handle_search(int conn_id, int32_t account_id,
                           const NetMsgFriendSearch *msg, LobbyDB *db)
{
    if (!check_search_rate(conn_id)) return;

    /* Validate query length >= 4 */
    size_t qlen = strnlen(msg->query, 32);
    if (qlen < 4) return;

    /* Build LIKE pattern: "query%" */
    char pattern[34];
    snprintf(pattern, sizeof(pattern), "%.*s%%", (int)qlen, msg->query);

    sqlite3_stmt *stmt = lobbydb_stmt(db, LOBBY_STMT_FRIEND_SEARCH);
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, account_id);

    NetMsg reply = { .type = NET_MSG_FRIEND_SEARCH_RESULT };
    NetMsgFriendSearchResult *res = &reply.friend_search_result;
    res->count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW && res->count < 10) {
        int idx = res->count;
        int32_t found_id = sqlite3_column_int(stmt, 0);
        const char *found_name = (const char *)sqlite3_column_text(stmt, 1);

        res->results[idx].account_id = found_id;
        strncpy(res->results[idx].username, found_name, 31);
        res->results[idx].username[31] = '\0';

        /* Determine relationship status */
        /* Check: already friends? */
        sqlite3_stmt *chk = lobbydb_stmt(db, LOBBY_STMT_FRIEND_CHECK);
        sqlite3_reset(chk);
        sqlite3_bind_int(chk, 1, account_id);
        sqlite3_bind_int(chk, 2, found_id);
        if (sqlite3_step(chk) == SQLITE_ROW) {
            res->results[idx].status = FRIEND_STATUS_ALREADY_FRIEND;
            res->count++;
            continue;
        }

        /* Check: I sent them a request? */
        sqlite3_stmt *fchk = lobbydb_stmt(db, LOBBY_STMT_FREQ_CHECK);
        sqlite3_reset(fchk);
        sqlite3_bind_int(fchk, 1, account_id);
        sqlite3_bind_int(fchk, 2, found_id);
        if (sqlite3_step(fchk) == SQLITE_ROW) {
            res->results[idx].status = FRIEND_STATUS_PENDING_SENT;
            res->count++;
            continue;
        }

        /* Check: they sent me a request? */
        sqlite3_reset(fchk);
        sqlite3_bind_int(fchk, 1, found_id);
        sqlite3_bind_int(fchk, 2, account_id);
        if (sqlite3_step(fchk) == SQLITE_ROW) {
            res->results[idx].status = FRIEND_STATUS_PENDING_RECEIVED;
            res->count++;
            continue;
        }

        /* Check: they blocked me? */
        sqlite3_stmt *bchk = lobbydb_stmt(db, LOBBY_STMT_FBLOCK_CHECK);
        sqlite3_reset(bchk);
        sqlite3_bind_int(bchk, 1, found_id);
        sqlite3_bind_int(bchk, 2, account_id);
        if (sqlite3_step(bchk) == SQLITE_ROW) {
            res->results[idx].status = FRIEND_STATUS_BLOCKED;
            res->count++;
            continue;
        }

        res->results[idx].status = FRIEND_STATUS_AVAILABLE;
        res->count++;
    }

    send_msg(conn_id, &reply);
}
```

- [ ] **Step 4: Implement friends_handle_request**

```c
void friends_handle_request(int conn_id, int32_t account_id,
                            const NetMsgFriendRequest *msg, LobbyDB *db)
{
    int32_t target = msg->target_account_id;

    /* Not self */
    if (target == account_id) return;

    /* Not already friends */
    sqlite3_stmt *chk = lobbydb_stmt(db, LOBBY_STMT_FRIEND_CHECK);
    sqlite3_reset(chk);
    sqlite3_bind_int(chk, 1, account_id);
    sqlite3_bind_int(chk, 2, target);
    if (sqlite3_step(chk) == SQLITE_ROW) return;

    /* Check if target already sent us a request — auto-accept */
    sqlite3_stmt *fchk = lobbydb_stmt(db, LOBBY_STMT_FREQ_CHECK);
    sqlite3_reset(fchk);
    sqlite3_bind_int(fchk, 1, target);
    sqlite3_bind_int(fchk, 2, account_id);
    if (sqlite3_step(fchk) == SQLITE_ROW) {
        /* Auto-accept: simulate accept of target's request */
        NetMsgFriendAccept auto_accept = {0};
        auto_accept.from_account_id = target;
        friends_handle_accept(conn_id, account_id, &auto_accept, db);
        return;
    }

    /* Not blocked by target */
    sqlite3_stmt *bchk = lobbydb_stmt(db, LOBBY_STMT_FBLOCK_CHECK);
    sqlite3_reset(bchk);
    sqlite3_bind_int(bchk, 1, target);
    sqlite3_bind_int(bchk, 2, account_id);
    if (sqlite3_step(bchk) == SQLITE_ROW) return;

    /* Sender < 10 outgoing pending */
    sqlite3_stmt *cnt = lobbydb_stmt(db, LOBBY_STMT_FREQ_COUNT_OUTGOING);
    sqlite3_reset(cnt);
    sqlite3_bind_int(cnt, 1, account_id);
    if (sqlite3_step(cnt) == SQLITE_ROW && sqlite3_column_int(cnt, 0) >= 10) return;

    /* Target < 20 friends */
    if (count_friends(db, target) >= 20) return;

    /* Insert request */
    sqlite3_stmt *ins = lobbydb_stmt(db, LOBBY_STMT_FREQ_INSERT);
    sqlite3_reset(ins);
    sqlite3_bind_int(ins, 1, account_id);
    sqlite3_bind_int(ins, 2, target);
    sqlite3_step(ins);

    /* Notify target if online */
    int target_conn = find_conn_for_account(target);
    if (target_conn >= 0) {
        NetMsg notify = { .type = NET_MSG_FRIEND_REQUEST_NOTIFY };
        get_username(db, account_id, notify.friend_request_notify.username);
        notify.friend_request_notify.account_id = account_id;
        send_msg(target_conn, &notify);
    }
}
```

- [ ] **Step 5: Implement friends_handle_accept**

```c
void friends_handle_accept(int conn_id, int32_t account_id,
                           const NetMsgFriendAccept *msg, LobbyDB *db)
{
    int32_t from = msg->from_account_id;

    /* Verify request exists */
    sqlite3_stmt *chk = lobbydb_stmt(db, LOBBY_STMT_FREQ_CHECK);
    sqlite3_reset(chk);
    sqlite3_bind_int(chk, 1, from);
    sqlite3_bind_int(chk, 2, account_id);
    if (sqlite3_step(chk) != SQLITE_ROW) return;

    /* Check both < 20 friends */
    if (count_friends(db, account_id) >= 20) return;
    if (count_friends(db, from) >= 20) return;

    /* Delete request */
    sqlite3_stmt *del = lobbydb_stmt(db, LOBBY_STMT_FREQ_DELETE);
    sqlite3_reset(del);
    sqlite3_bind_int(del, 1, from);
    sqlite3_bind_int(del, 2, account_id);
    sqlite3_step(del);

    /* Insert friendship (canonical order) */
    int32_t a = (from < account_id) ? from : account_id;
    int32_t b = (from < account_id) ? account_id : from;
    sqlite3_stmt *ins = lobbydb_stmt(db, LOBBY_STMT_FRIEND_INSERT);
    sqlite3_reset(ins);
    sqlite3_bind_int(ins, 1, a);
    sqlite3_bind_int(ins, 2, b);
    sqlite3_step(ins);

    /* Notify the original sender if online */
    int from_conn = find_conn_for_account(from);
    if (from_conn >= 0) {
        NetMsg update = { .type = NET_MSG_FRIEND_UPDATE };
        update.friend_update.account_id = account_id;
        update.friend_update.presence = friends_get_presence(account_id);
        send_msg(from_conn, &update);
    }

    /* Send presence of the new friend back to accepter */
    NetMsg update = { .type = NET_MSG_FRIEND_UPDATE };
    update.friend_update.account_id = from;
    update.friend_update.presence = friends_get_presence(from);
    send_msg(conn_id, &update);
}
```

- [ ] **Step 6: Implement friends_handle_reject**

```c
void friends_handle_reject(int conn_id, int32_t account_id,
                           const NetMsgFriendReject *msg, LobbyDB *db)
{
    int32_t from = msg->from_account_id;

    /* Verify request exists */
    sqlite3_stmt *chk = lobbydb_stmt(db, LOBBY_STMT_FREQ_CHECK);
    sqlite3_reset(chk);
    sqlite3_bind_int(chk, 1, from);
    sqlite3_bind_int(chk, 2, account_id);
    if (sqlite3_step(chk) != SQLITE_ROW) return;

    /* Delete request */
    sqlite3_stmt *del = lobbydb_stmt(db, LOBBY_STMT_FREQ_DELETE);
    sqlite3_reset(del);
    sqlite3_bind_int(del, 1, from);
    sqlite3_bind_int(del, 2, account_id);
    sqlite3_step(del);

    /* Insert block */
    sqlite3_stmt *blk = lobbydb_stmt(db, LOBBY_STMT_FBLOCK_INSERT);
    sqlite3_reset(blk);
    sqlite3_bind_int(blk, 1, account_id);  /* blocker */
    sqlite3_bind_int(blk, 2, from);         /* blocked */
    sqlite3_step(blk);
}
```

- [ ] **Step 7: Implement friends_handle_remove**

```c
void friends_handle_remove(int conn_id, int32_t account_id,
                           const NetMsgFriendRemove *msg, LobbyDB *db)
{
    int32_t target = msg->target_account_id;

    /* Delete friendship (try both orderings) */
    sqlite3_stmt *del = lobbydb_stmt(db, LOBBY_STMT_FRIEND_DELETE);
    sqlite3_reset(del);
    sqlite3_bind_int(del, 1, account_id);
    sqlite3_bind_int(del, 2, target);
    sqlite3_bind_int(del, 3, target);
    sqlite3_bind_int(del, 4, account_id);
    sqlite3_step(del);

    /* Notify target if online — presence 0xFF means "removed" */
    int target_conn = find_conn_for_account(target);
    if (target_conn >= 0) {
        NetMsg update = { .type = NET_MSG_FRIEND_UPDATE };
        update.friend_update.account_id = account_id;
        update.friend_update.presence = 0xFF;
        send_msg(target_conn, &update);
    }

    /* Clean up pending room invites involving this pair */
    for (int i = 0; i < MAX_PENDING_INVITES; i++) {
        if (!g_invites[i].active) continue;
        if ((g_invites[i].inviter_account_id == account_id && g_invites[i].invitee_account_id == target) ||
            (g_invites[i].inviter_account_id == target && g_invites[i].invitee_account_id == account_id)) {
            /* Expire invite */
            int invitee_conn = find_conn_for_account(g_invites[i].invitee_account_id);
            if (invitee_conn >= 0) {
                NetMsg expire = { .type = NET_MSG_ROOM_INVITE_EXPIRED };
                strncpy(expire.room_invite_expired.room_code, g_invites[i].room_code, 7);
                send_msg(invitee_conn, &expire);
            }
            g_invites[i].active = false;
        }
    }
}
```

- [ ] **Step 8: Implement friends_handle_list_request and presence functions**

```c
void friends_handle_list_request(int conn_id, int32_t account_id,
                                 const NetMsgFriendListRequest *msg, LobbyDB *db)
{
    (void)msg; /* auth_token already validated by caller */

    NetMsg reply = { .type = NET_MSG_FRIEND_LIST };
    NetMsgFriendList *fl = &reply.friend_list;
    fl->friend_count = 0;
    fl->request_count = 0;

    /* Query friends */
    sqlite3_stmt *stmt = lobbydb_stmt(db, LOBBY_STMT_FRIEND_LIST);
    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, account_id);

    while (sqlite3_step(stmt) == SQLITE_ROW && fl->friend_count < 20) {
        int idx = fl->friend_count;
        int32_t fid = sqlite3_column_int(stmt, 0);
        fl->friends[idx].account_id = fid;
        get_username(db, fid, fl->friends[idx].username);
        fl->friends[idx].presence = friends_get_presence(fid);
        fl->friend_count++;
    }

    /* Query incoming friend requests */
    sqlite3_stmt *req_stmt = lobbydb_stmt(db, LOBBY_STMT_FREQ_LIST_INCOMING);
    sqlite3_reset(req_stmt);
    sqlite3_bind_int(req_stmt, 1, account_id);

    while (sqlite3_step(req_stmt) == SQLITE_ROW && fl->request_count < 10) {
        int idx = fl->request_count;
        fl->incoming_requests[idx].account_id = sqlite3_column_int(req_stmt, 0);
        const char *name = (const char *)sqlite3_column_text(req_stmt, 1);
        strncpy(fl->incoming_requests[idx].username, name, 31);
        fl->incoming_requests[idx].username[31] = '\0';
        fl->request_count++;
    }

    send_msg(conn_id, &reply);
}

uint8_t friends_get_presence(int32_t account_id)
{
    /* Check in-game first */
    for (int i = 0; i < g_ingame_count; i++) {
        if (g_ingame[i].account_id == account_id)
            return FRIEND_PRESENCE_IN_GAME;
    }
    /* Check online (has active lobby connection) */
    if (find_conn_for_account(account_id) >= 0)
        return FRIEND_PRESENCE_ONLINE;
    return FRIEND_PRESENCE_OFFLINE;
}

void friends_on_player_authenticated(int conn_id, int32_t account_id, LobbyDB *db)
{
    /* Send friend list to newly authenticated player */
    NetMsgFriendListRequest dummy = {0};
    friends_handle_list_request(conn_id, account_id, &dummy, db);

    /* Notify online friends that this player came online */
    int32_t friend_ids[20];
    int friend_count = get_friend_ids(db, account_id, friend_ids, 20);
    for (int i = 0; i < friend_count; i++) {
        int fc = find_conn_for_account(friend_ids[i]);
        if (fc >= 0) {
            NetMsg update = { .type = NET_MSG_FRIEND_UPDATE };
            update.friend_update.account_id = account_id;
            update.friend_update.presence = FRIEND_PRESENCE_ONLINE;
            send_msg(fc, &update);
        }
    }
}

void friends_on_player_disconnected(int32_t account_id, LobbyDB *db)
{
    int32_t friend_ids[20];
    int friend_count = get_friend_ids(db, account_id, friend_ids, 20);
    for (int i = 0; i < friend_count; i++) {
        int fc = find_conn_for_account(friend_ids[i]);
        if (fc >= 0) {
            NetMsg update = { .type = NET_MSG_FRIEND_UPDATE };
            update.friend_update.account_id = account_id;
            /* Check if going to in-game or truly offline */
            update.friend_update.presence = friends_get_presence(account_id);
            send_msg(fc, &update);
        }
    }
}
```

- [ ] **Step 9: Implement room lifecycle presence hooks**

```c
void friends_on_room_created(const char *room_code, const int32_t *account_ids,
                             int count, LobbyDB *db)
{
    for (int i = 0; i < count; i++) {
        if (account_ids[i] <= 0) continue;
        /* Add to in-game map */
        if (g_ingame_count < FRIENDS_MAX_INGAME) {
            g_ingame[g_ingame_count].account_id = account_ids[i];
            strncpy(g_ingame[g_ingame_count].room_code, room_code, 7);
            g_ingame[g_ingame_count].room_code[7] = '\0';
            g_ingame_count++;
        }
        /* Notify their friends */
        int32_t friend_ids[20];
        int fc = get_friend_ids(db, account_ids[i], friend_ids, 20);
        for (int j = 0; j < fc; j++) {
            int conn = find_conn_for_account(friend_ids[j]);
            if (conn >= 0) {
                NetMsg update = { .type = NET_MSG_FRIEND_UPDATE };
                update.friend_update.account_id = account_ids[i];
                update.friend_update.presence = FRIEND_PRESENCE_IN_GAME;
                send_msg(conn, &update);
            }
        }
    }
}

void friends_on_room_destroyed(const char *room_code, LobbyDB *db)
{
    /* Collect account_ids being removed, then notify friends */
    int32_t removed[4];
    int removed_count = 0;

    for (int i = g_ingame_count - 1; i >= 0; i--) {
        if (strncmp(g_ingame[i].room_code, room_code, 7) == 0) {
            if (removed_count < 4) removed[removed_count++] = g_ingame[i].account_id;
            /* Swap-remove */
            g_ingame[i] = g_ingame[g_ingame_count - 1];
            g_ingame_count--;
        }
    }

    for (int i = 0; i < removed_count; i++) {
        uint8_t new_presence = friends_get_presence(removed[i]);
        int32_t friend_ids[20];
        int fc = get_friend_ids(db, removed[i], friend_ids, 20);
        for (int j = 0; j < fc; j++) {
            int conn = find_conn_for_account(friend_ids[j]);
            if (conn >= 0) {
                NetMsg update = { .type = NET_MSG_FRIEND_UPDATE };
                update.friend_update.account_id = removed[i];
                update.friend_update.presence = new_presence;
                send_msg(conn, &update);
            }
        }
    }
}

void friends_on_server_dead(int server_conn_id)
{
    /* Clear all in-game entries — we don't track which server owns which room here,
       so we'd need to clear all rooms associated with that server.
       The caller (lby_on_dead_server) should call friends_on_room_destroyed for each room. */
    (void)server_conn_id;
}
```

- [ ] **Step 10: Implement room invite handler**

```c
void friends_handle_room_invite(int conn_id, int32_t account_id,
                                const NetMsgRoomInvite *msg, LobbyDB *db)
{
    int32_t target = msg->target_account_id;

    /* Verify they're friends */
    sqlite3_stmt *chk = lobbydb_stmt(db, LOBBY_STMT_FRIEND_CHECK);
    sqlite3_reset(chk);
    sqlite3_bind_int(chk, 1, account_id);
    sqlite3_bind_int(chk, 2, target);
    if (sqlite3_step(chk) != SQLITE_ROW) return;

    /* Verify target is online (green dot — not in-game) */
    if (friends_get_presence(target) != FRIEND_PRESENCE_ONLINE) return;

    int target_conn = find_conn_for_account(target);
    if (target_conn < 0) return;

    /* Find inviter's room code. The inviter should be in a room that's in the
       room_codes table. We need to look up which room this player created/joined.
       The caller should provide this context, or we look it up.
       For now, check in-game map — but the inviter is in lobby (online), not in-game.
       The inviter is in a room lobby (waiting room) which is tracked differently.
       We need the room code from the pending room system. */

    /* Look up the inviter's active room from room_codes DB */
    /* NOTE: The implementer will need to determine how to get the inviter's
       current room code. Options:
       1. Add room_code field to LobbyConnInfo
       2. Query room_codes table
       3. Pass room_code in the NetMsgRoomInvite struct
       Recommendation: Add room_code[8] to NetMsgRoomInvite struct for simplicity. */

    /* Store pending invite */
    for (int i = 0; i < MAX_PENDING_INVITES; i++) {
        if (!g_invites[i].active) {
            g_invites[i].active = true;
            g_invites[i].inviter_account_id = account_id;
            g_invites[i].invitee_account_id = target;
            /* room_code comes from inviter's known room — see note above */
            g_invites[i].active = true;
            break;
        }
    }

    /* Send invite notification */
    NetMsg notify = { .type = NET_MSG_ROOM_INVITE_NOTIFY };
    get_username(db, account_id, notify.room_invite_notify.from_username);
    /* Copy room_code — see note above about source */
    send_msg(target_conn, &notify);
}

void friends_expire_room_invites(const char *room_code)
{
    for (int i = 0; i < MAX_PENDING_INVITES; i++) {
        if (!g_invites[i].active) continue;
        if (strncmp(g_invites[i].room_code, room_code, 7) != 0) continue;

        int conn = find_conn_for_account(g_invites[i].invitee_account_id);
        if (conn >= 0) {
            NetMsg expire = { .type = NET_MSG_ROOM_INVITE_EXPIRED };
            strncpy(expire.room_invite_expired.room_code, room_code, 7);
            expire.room_invite_expired.room_code[7] = '\0';
            send_msg(conn, &expire);
        }
        g_invites[i].active = false;
    }
}
```

- [ ] **Step 11: Build lobby and verify**

Run: `make lobby`
Expected: Clean build, no errors. (Linker may warn about `find_conn_for_account` — that's expected until Task 5.)

- [ ] **Step 12: Commit**

```bash
git add src/lobby/friends.h src/lobby/friends.c
git commit -m "feat(lobby): add friends module — search, request, accept, reject, remove, presence, room invites"
```

---

### Task 5: Lobby Net Integration — Dispatch and Hooks

**Files:**
- Modify: `src/lobby/lobby_net.c:238-277,335-370` (message dispatch, cleanup)
- Modify: `src/lobby/lobby_net.h` (add `lobby_net_find_conn_by_account`)

- [ ] **Step 1: Add lobby_net_find_conn_by_account**

In `src/lobby/lobby_net.h`, add:

```c
/* Find connection ID for an authenticated account_id. Returns -1 if not found. */
int lobby_net_find_conn_by_account(int32_t account_id);
```

In `src/lobby/lobby_net.c`, implement it (needs access to `g_conn_info` array — the `LobbyConnInfo` file-scope array):

```c
int lobby_net_find_conn_by_account(int32_t account_id)
{
    if (account_id <= 0) return -1;
    for (int i = 0; i < g_net.max_conns; i++) {
        if (g_net.conns[i].state == NET_CONN_CONNECTED &&
            g_conn_info[i].auth_state == LOBBY_AUTH_AUTHENTICATED &&
            g_conn_info[i].account_id == account_id) {
            return i;
        }
    }
    return -1;
}
```

Note: the implementer should verify the exact name of the `LobbyConnInfo` array in `lobby_net.c` (it's likely `g_conn_info` or similar — check the file-scope declarations).

- [ ] **Step 2: Add friend message dispatch cases**

In `src/lobby/lobby_net.c`, include `"friends.h"` at the top.

In `lby_handle_message`, add cases after `NET_MSG_LEADERBOARD_REQUEST`:

```c
    /* Friend system messages */
    case NET_MSG_FRIEND_SEARCH:
        if (ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_search(conn_id, ci->account_id, &msg->friend_search, g_db);
        break;
    case NET_MSG_FRIEND_REQUEST:
        if (ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_request(conn_id, ci->account_id, &msg->friend_request, g_db);
        break;
    case NET_MSG_FRIEND_ACCEPT:
        if (ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_accept(conn_id, ci->account_id, &msg->friend_accept, g_db);
        break;
    case NET_MSG_FRIEND_REJECT:
        if (ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_reject(conn_id, ci->account_id, &msg->friend_reject, g_db);
        break;
    case NET_MSG_FRIEND_REMOVE:
        if (ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_remove(conn_id, ci->account_id, &msg->friend_remove, g_db);
        break;
    case NET_MSG_FRIEND_LIST_REQUEST:
        if (ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_list_request(conn_id, ci->account_id, &msg->friend_list_request, g_db);
        break;
    case NET_MSG_ROOM_INVITE:
        if (ci->auth_state == LOBBY_AUTH_AUTHENTICATED)
            friends_handle_room_invite(conn_id, ci->account_id, &msg->room_invite, g_db);
        break;
```

- [ ] **Step 3: Hook into authentication success**

In `lby_handle_login_response` (the function that completes authentication), after the line that sends `NET_MSG_LOGIN_ACK`, add:

```c
    friends_on_player_authenticated(conn_id, ci->account_id, g_db);
```

- [ ] **Step 4: Hook into disconnect cleanup**

In `lby_cleanup_connection`, before resetting the connection info, add:

```c
    if (ci->auth_state == LOBBY_AUTH_AUTHENTICATED && ci->account_id > 0) {
        friends_on_player_disconnected(ci->account_id, g_db);
    }
```

- [ ] **Step 5: Hook into room lifecycle**

In `lby_handle_server_room_created`, after processing the room, extract the player account_ids from the tokens and call:

```c
    friends_on_room_created(room_code, account_ids, player_count, g_db);
```

The implementer must resolve the player_tokens to account_ids using `auth_validate_token`.

In `lby_handle_server_room_destroyed`, call:

```c
    friends_on_room_destroyed(msg->room_code, g_db);
    friends_expire_room_invites(msg->room_code);
```

- [ ] **Step 6: Initialize friends module**

In `lobby_net_init`, after existing init code, add:

```c
    friends_init();
```

- [ ] **Step 7: Build lobby and verify**

Run: `make lobby`
Expected: Clean build, no errors.

- [ ] **Step 8: Commit**

```bash
git add src/lobby/lobby_net.c src/lobby/lobby_net.h
git commit -m "feat(lobby): integrate friend dispatch, auth/disconnect hooks, room lifecycle hooks"
```

---

### Task 6: Client Lobby — Friend Send/Receive Functions

**Files:**
- Modify: `src/net/lobby_client.h:136-142`
- Modify: `src/net/lobby_client.c:172-177`

- [ ] **Step 1: Add friend state and API to lobby_client.h**

In `src/net/lobby_client.h`, add after the leaderboard functions:

```c
/* ================================================================
 * Friend System
 * ================================================================ */

/* Outbound actions */
void lobby_client_friend_search(const char *query);
void lobby_client_friend_request(int32_t account_id);
void lobby_client_friend_accept(int32_t from_account_id);
void lobby_client_friend_reject(int32_t from_account_id);
void lobby_client_friend_remove(int32_t account_id);
void lobby_client_friend_list_request(void);
void lobby_client_room_invite(int32_t account_id);

/* Inbound polling — returns true and fills output if a new message arrived */
bool lobby_client_has_friend_list(NetMsgFriendList *out);
bool lobby_client_has_friend_search_result(NetMsgFriendSearchResult *out);
bool lobby_client_has_friend_update(NetMsgFriendUpdate *out);
bool lobby_client_has_friend_request_notify(NetMsgFriendRequestNotify *out);
bool lobby_client_has_room_invite_notify(NetMsgRoomInviteNotify *out);
bool lobby_client_has_room_invite_expired(NetMsgRoomInviteExpired *out);
```

- [ ] **Step 2: Add file-scope state for friend messages in lobby_client.c**

In `src/net/lobby_client.c`, add file-scope variables:

```c
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
```

- [ ] **Step 3: Add send functions**

```c
void lobby_client_friend_search(const char *query)
{
    NetMsg msg = { .type = NET_MSG_FRIEND_SEARCH };
    memcpy(msg.friend_search.auth_token, g_info.auth_token, 32);
    strncpy(msg.friend_search.query, query, 31);
    msg.friend_search.query[31] = '\0';
    lby_send(&msg);
}

void lobby_client_friend_request(int32_t account_id)
{
    NetMsg msg = { .type = NET_MSG_FRIEND_REQUEST };
    memcpy(msg.friend_request.auth_token, g_info.auth_token, 32);
    msg.friend_request.target_account_id = account_id;
    lby_send(&msg);
}

void lobby_client_friend_accept(int32_t from_account_id)
{
    NetMsg msg = { .type = NET_MSG_FRIEND_ACCEPT };
    memcpy(msg.friend_accept.auth_token, g_info.auth_token, 32);
    msg.friend_accept.from_account_id = from_account_id;
    lby_send(&msg);
}

void lobby_client_friend_reject(int32_t from_account_id)
{
    NetMsg msg = { .type = NET_MSG_FRIEND_REJECT };
    memcpy(msg.friend_reject.auth_token, g_info.auth_token, 32);
    msg.friend_reject.from_account_id = from_account_id;
    lby_send(&msg);
}

void lobby_client_friend_remove(int32_t account_id)
{
    NetMsg msg = { .type = NET_MSG_FRIEND_REMOVE };
    memcpy(msg.friend_remove.auth_token, g_info.auth_token, 32);
    msg.friend_remove.target_account_id = account_id;
    lby_send(&msg);
}

void lobby_client_friend_list_request(void)
{
    NetMsg msg = { .type = NET_MSG_FRIEND_LIST_REQUEST };
    memcpy(msg.friend_list_request.auth_token, g_info.auth_token, 32);
    lby_send(&msg);
}

void lobby_client_room_invite(int32_t account_id)
{
    NetMsg msg = { .type = NET_MSG_ROOM_INVITE };
    memcpy(msg.room_invite.auth_token, g_info.auth_token, 32);
    msg.room_invite.target_account_id = account_id;
    lby_send(&msg);
}
```

- [ ] **Step 4: Add receive handling in message dispatch**

In the lobby_client receive handler (the switch in `lobby_client_update` that processes incoming messages), add:

```c
    case NET_MSG_FRIEND_LIST:
        g_friend_list = msg.friend_list;
        g_has_friend_list = true;
        break;

    case NET_MSG_FRIEND_SEARCH_RESULT:
        g_search_result = msg.friend_search_result;
        g_has_search_result = true;
        break;

    case NET_MSG_FRIEND_UPDATE:
        if (g_friend_update_count < MAX_FRIEND_UPDATES)
            g_friend_updates[g_friend_update_count++] = msg.friend_update;
        break;

    case NET_MSG_FRIEND_REQUEST_NOTIFY:
        if (g_friend_req_notif_count < MAX_FRIEND_REQ_NOTIFS)
            g_friend_req_notifs[g_friend_req_notif_count++] = msg.friend_request_notify;
        break;

    case NET_MSG_ROOM_INVITE_NOTIFY:
        g_room_invite = msg.room_invite_notify;
        g_has_room_invite = true;
        break;

    case NET_MSG_ROOM_INVITE_EXPIRED:
        g_room_invite_expired = msg.room_invite_expired;
        g_has_room_invite_expired = true;
        break;
```

- [ ] **Step 5: Add polling functions**

```c
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
    /* Shift remaining */
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
```

- [ ] **Step 6: Build all and verify**

Run: `make debug-all`
Expected: Clean build, no errors.

- [ ] **Step 7: Commit**

```bash
git add src/net/lobby_client.h src/net/lobby_client.c
git commit -m "feat(client): add friend system send/receive functions in lobby_client"
```

---

### Task 7: Client Friend Panel — State and Logic

**Files:**
- Create: `src/game/friend_panel.h`
- Create: `src/game/friend_panel.c`

- [ ] **Step 1: Create friend_panel.h**

```c
#ifndef FRIEND_PANEL_H
#define FRIEND_PANEL_H

#include "../net/protocol.h"
#include <stdbool.h>
#include <stdint.h>

/* ================================================================
 * Types
 * ================================================================ */

typedef enum {
    FRIEND_ENTRY_INVITE,    /* Room invitation — top of list */
    FRIEND_ENTRY_REQUEST,   /* Friend request — below invites */
    FRIEND_ENTRY_FRIEND,    /* Confirmed friend */
} FriendEntryType;

typedef struct {
    FriendEntryType type;
    char    username[32];
    int32_t account_id;
    uint8_t presence;       /* FriendPresence — friends only */
    char    room_code[8];   /* room invites only */
} FriendEntry;

typedef struct {
    /* Sorted entry list (invites, requests, online friends, in-game, offline) */
    FriendEntry entries[32];
    int         entry_count;

    /* Search bar state */
    char    search_buf[32];
    int     search_len;
    bool    search_active;  /* true when search bar is focused */
    struct {
        char    username[32];
        int32_t account_id;
        uint8_t status;
    } search_results[10];
    int     search_result_count;
    bool    search_results_visible;

    /* Scroll state */
    float   scroll_offset;
    float   scroll_target;

    /* Context menu (right-click) */
    bool    context_menu_open;
    int     context_menu_entry;  /* index into entries[] */
    float   context_menu_x;
    float   context_menu_y;

    /* Remove confirmation dialog */
    bool    confirm_remove_open;
    int     confirm_remove_entry;

    /* Room invite capability */
    bool    can_invite;
} FriendPanelState;

/* ================================================================
 * API
 * ================================================================ */

void friend_panel_init(FriendPanelState *state);
void friend_panel_update(FriendPanelState *state, float dt);
void friend_panel_set_can_invite(FriendPanelState *state, bool can_invite);

#endif /* FRIEND_PANEL_H */
```

- [ ] **Step 2: Create friend_panel.c**

```c
#include "friend_panel.h"
#include "../net/lobby_client.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Sorting helper — invites first, then requests, then online, in-game, offline
 * ================================================================ */

static int entry_sort_key(const FriendEntry *e)
{
    if (e->type == FRIEND_ENTRY_INVITE)  return 0;
    if (e->type == FRIEND_ENTRY_REQUEST) return 1;
    if (e->type == FRIEND_ENTRY_FRIEND) {
        if (e->presence == FRIEND_PRESENCE_ONLINE)  return 2;
        if (e->presence == FRIEND_PRESENCE_IN_GAME) return 3;
        return 4; /* offline */
    }
    return 5;
}

static int compare_entries(const void *a, const void *b)
{
    int ka = entry_sort_key((const FriendEntry *)a);
    int kb = entry_sort_key((const FriendEntry *)b);
    return ka - kb;
}

static void sort_entries(FriendPanelState *state)
{
    qsort(state->entries, state->entry_count, sizeof(FriendEntry), compare_entries);
}

/* ================================================================
 * Apply incoming network updates
 * ================================================================ */

static void apply_friend_list(FriendPanelState *state, const NetMsgFriendList *fl)
{
    state->entry_count = 0;

    /* Add incoming friend requests */
    for (int i = 0; i < fl->request_count && state->entry_count < 32; i++) {
        FriendEntry *e = &state->entries[state->entry_count++];
        e->type = FRIEND_ENTRY_REQUEST;
        strncpy(e->username, fl->incoming_requests[i].username, 31);
        e->username[31] = '\0';
        e->account_id = fl->incoming_requests[i].account_id;
        e->presence = 0;
        e->room_code[0] = '\0';
    }

    /* Add friends */
    for (int i = 0; i < fl->friend_count && state->entry_count < 32; i++) {
        FriendEntry *e = &state->entries[state->entry_count++];
        e->type = FRIEND_ENTRY_FRIEND;
        strncpy(e->username, fl->friends[i].username, 31);
        e->username[31] = '\0';
        e->account_id = fl->friends[i].account_id;
        e->presence = fl->friends[i].presence;
        e->room_code[0] = '\0';
    }

    sort_entries(state);
}

static void apply_friend_update(FriendPanelState *state, const NetMsgFriendUpdate *upd)
{
    if (upd->presence == 0xFF) {
        /* Friend removed — delete from list */
        for (int i = 0; i < state->entry_count; i++) {
            if (state->entries[i].account_id == upd->account_id &&
                state->entries[i].type == FRIEND_ENTRY_FRIEND) {
                /* Shift remaining */
                for (int j = i; j < state->entry_count - 1; j++)
                    state->entries[j] = state->entries[j + 1];
                state->entry_count--;
                break;
            }
        }
        sort_entries(state);
        return;
    }

    /* Check if this is an existing friend — update presence */
    for (int i = 0; i < state->entry_count; i++) {
        if (state->entries[i].account_id == upd->account_id &&
            state->entries[i].type == FRIEND_ENTRY_FRIEND) {
            state->entries[i].presence = upd->presence;
            sort_entries(state);
            return;
        }
    }

    /* New friend (just accepted) — add to list.
       We don't have the username yet; request a full refresh. */
    lobby_client_friend_list_request();
}

static void apply_request_notify(FriendPanelState *state, const NetMsgFriendRequestNotify *rn)
{
    if (state->entry_count >= 32) return;
    FriendEntry *e = &state->entries[state->entry_count++];
    e->type = FRIEND_ENTRY_REQUEST;
    strncpy(e->username, rn->username, 31);
    e->username[31] = '\0';
    e->account_id = rn->account_id;
    e->presence = 0;
    e->room_code[0] = '\0';
    sort_entries(state);
}

static void apply_room_invite(FriendPanelState *state, const NetMsgRoomInviteNotify *inv)
{
    if (state->entry_count >= 32) return;
    FriendEntry *e = &state->entries[state->entry_count++];
    e->type = FRIEND_ENTRY_INVITE;
    strncpy(e->username, inv->from_username, 31);
    e->username[31] = '\0';
    e->account_id = 0; /* not needed for display */
    e->presence = 0;
    strncpy(e->room_code, inv->room_code, 7);
    e->room_code[7] = '\0';
    sort_entries(state);
}

static void apply_room_invite_expired(FriendPanelState *state, const NetMsgRoomInviteExpired *exp)
{
    for (int i = 0; i < state->entry_count; i++) {
        if (state->entries[i].type == FRIEND_ENTRY_INVITE &&
            strncmp(state->entries[i].room_code, exp->room_code, 7) == 0) {
            for (int j = i; j < state->entry_count - 1; j++)
                state->entries[j] = state->entries[j + 1];
            state->entry_count--;
            break;
        }
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

void friend_panel_init(FriendPanelState *state)
{
    memset(state, 0, sizeof(*state));
}

void friend_panel_update(FriendPanelState *state, float dt)
{
    (void)dt;

    /* Poll for network updates */
    NetMsgFriendList fl;
    if (lobby_client_has_friend_list(&fl))
        apply_friend_list(state, &fl);

    NetMsgFriendSearchResult sr;
    if (lobby_client_has_friend_search_result(&sr)) {
        state->search_result_count = sr.count;
        for (int i = 0; i < sr.count; i++) {
            strncpy(state->search_results[i].username, sr.results[i].username, 31);
            state->search_results[i].username[31] = '\0';
            state->search_results[i].account_id = sr.results[i].account_id;
            state->search_results[i].status = sr.results[i].status;
        }
        state->search_results_visible = (sr.count > 0);
    }

    NetMsgFriendUpdate fu;
    while (lobby_client_has_friend_update(&fu))
        apply_friend_update(state, &fu);

    NetMsgFriendRequestNotify rn;
    while (lobby_client_has_friend_request_notify(&rn))
        apply_request_notify(state, &rn);

    NetMsgRoomInviteNotify inv;
    if (lobby_client_has_room_invite_notify(&inv))
        apply_room_invite(state, &inv);

    NetMsgRoomInviteExpired exp;
    if (lobby_client_has_room_invite_expired(&exp))
        apply_room_invite_expired(state, &exp);

    /* Smooth scroll */
    float diff = state->scroll_target - state->scroll_offset;
    if (diff > 0.5f || diff < -0.5f)
        state->scroll_offset += diff * 0.15f;
    else
        state->scroll_offset = state->scroll_target;
}

void friend_panel_set_can_invite(FriendPanelState *state, bool can_invite)
{
    state->can_invite = can_invite;
}
```

- [ ] **Step 3: Build all and verify**

Run: `make debug-all`
Expected: Clean build, no errors.

- [ ] **Step 4: Commit**

```bash
git add src/game/friend_panel.h src/game/friend_panel.c
git commit -m "feat(client): add friend panel state management and network update logic"
```

---

### Task 8: Client Friend Panel — Rendering

**Files:**
- Create: `src/render/friend_panel_render.h`
- Create: `src/render/friend_panel_render.c`

This task creates the visual rendering of the friend panel. The implementer should reference the existing rendering patterns in `src/render/render.c` for font access, color scheme, button drawing, and layout conventions.

- [ ] **Step 1: Create friend_panel_render.h**

```c
#ifndef FRIEND_PANEL_RENDER_H
#define FRIEND_PANEL_RENDER_H

#include "../game/friend_panel.h"
#include <raylib.h>

/* Panel dimensions */
#define FRIEND_PANEL_WIDTH  220
#define FRIEND_ENTRY_HEIGHT 36
#define FRIEND_SEARCH_HEIGHT 32

/* Draw the friend panel. Returns true if any button was clicked
   (side effects via lobby_client calls handled internally). */
void friend_panel_render_draw(FriendPanelState *state, Rectangle panel_rect, Font font);

/* Process input for the friend panel (mouse clicks, text input, scroll).
   Call before draw. */
void friend_panel_render_input(FriendPanelState *state, Rectangle panel_rect);

#endif /* FRIEND_PANEL_RENDER_H */
```

- [ ] **Step 2: Create friend_panel_render.c**

The implementer should build this file following these specifications:

```c
#include "friend_panel_render.h"
#include "../net/lobby_client.h"
#include <raylib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Colors
 * ================================================================ */

#define COL_PANEL_BG      (Color){30, 30, 40, 240}
#define COL_ENTRY_BG      (Color){40, 40, 55, 255}
#define COL_ENTRY_HOVER   (Color){50, 50, 70, 255}
#define COL_TEXT           (Color){220, 220, 220, 255}
#define COL_TEXT_DIM       (Color){140, 140, 160, 255}
#define COL_GREEN_DOT      (Color){80, 220, 80, 255}
#define COL_YELLOW_DOT     (Color){220, 200, 50, 255}
#define COL_BTN_ACCEPT     (Color){50, 150, 80, 255}
#define COL_BTN_REJECT     (Color){180, 50, 50, 255}
#define COL_BTN_INVITE     (Color){50, 100, 180, 255}
#define COL_BTN_ADD        (Color){50, 150, 80, 255}
#define COL_BTN_BLOCKED    (Color){80, 80, 80, 255}
#define COL_SEARCH_BG      (Color){25, 25, 35, 255}
#define COL_LABEL_REQUEST  (Color){220, 180, 50, 255}
#define COL_LABEL_INVITE   (Color){100, 180, 255, 255}
#define COL_CONTEXT_BG     (Color){50, 50, 65, 250}
#define COL_CONFIRM_BG     (Color){40, 40, 55, 250}
```

Key rendering functions the implementer must build:

**`friend_panel_render_draw`** — draws in this order:
1. Panel background (semi-transparent dark rectangle)
2. Title "Friends" at top
3. Scrollable area with entries, clipped to panel bounds:
   - Each entry: background rect, username text, type-specific elements
   - `FRIEND_ENTRY_INVITE`: blue "Room Invitation" label, Accept/Reject buttons
   - `FRIEND_ENTRY_REQUEST`: yellow "Friend Request" label, Accept/Reject buttons
   - `FRIEND_ENTRY_FRIEND`: presence dot (green/yellow/none), Invite button if `can_invite` and presence is online
4. Search bar at bottom (text input field)
5. Search results (rolling upward from search bar) if visible
6. Context menu overlay if open
7. Confirmation dialog overlay if open

**`friend_panel_render_input`** — handles:
1. Mouse wheel scroll within panel bounds
2. Click on Accept/Reject buttons → call `lobby_client_friend_accept/reject/join_room`
3. Click on Invite button → call `lobby_client_room_invite`
4. Right-click on friend entry → open context menu
5. Click "Remove Friend" in context menu → open confirmation dialog
6. Click Yes/No in confirmation → call `lobby_client_friend_remove` or close
7. Click on search bar → activate text input
8. Text input in search bar → update `search_buf`, send search when >=4 chars
9. Click on search result "Add" button → call `lobby_client_friend_request`
10. Click outside search results → close results
11. Click outside context menu → close context menu

For room invite acceptance: when player clicks Accept on a room invite entry, call `lobby_client_join_room(entry->room_code)` and remove the entry from the list.

- [ ] **Step 3: Build all and verify**

Run: `make debug-all`
Expected: Clean build, no errors.

- [ ] **Step 4: Commit**

```bash
git add src/render/friend_panel_render.h src/render/friend_panel_render.c
git commit -m "feat(render): add friend panel rendering — entries, search, context menu, confirmation dialog"
```

---

### Task 9: Integration — Wire Friend Panel into Online UI

**Files:**
- Modify: `src/game/online_ui.h:35-76` (add FriendPanelState to OnlineUIState)
- Modify: `src/game/online_ui.c` (init, update, integrate)
- Modify: `src/render/render.c` (draw friend panel in online menu)

- [ ] **Step 1: Add FriendPanelState to OnlineUIState**

In `src/game/online_ui.h`, add include and field:

```c
#include "friend_panel.h"
```

Add to `OnlineUIState` struct:

```c
    FriendPanelState friend_panel;
```

- [ ] **Step 2: Initialize friend panel in online_ui_init**

In `src/game/online_ui.c`, in `online_ui_init`:

```c
    friend_panel_init(&state->friend_panel);
```

- [ ] **Step 3: Update friend panel in online UI update**

In the online UI update path (wherever `OnlineUIState` is updated each frame), add:

```c
    friend_panel_update(&state->friend_panel, dt);
    friend_panel_set_can_invite(&state->friend_panel,
        state->subphase == ONLINE_SUB_CONNECTED_WAITING);
```

- [ ] **Step 4: Integrate rendering in render.c**

In `src/render/render.c`, in the online menu draw path, add the friend panel draw call. The panel should be positioned on the left side of the screen:

```c
    #include "friend_panel_render.h"

    /* In the online menu render section: */
    Rectangle friend_rect = {
        0,                          /* left edge */
        /* top offset below title bar or header */
        60,
        FRIEND_PANEL_WIDTH,
        GetScreenHeight() - 120     /* leave room for search bar at bottom */
    };
    friend_panel_render_input(&online_ui->friend_panel, friend_rect);
    friend_panel_render_draw(&online_ui->friend_panel, friend_rect, font);
```

The exact positioning values should be adjusted to match the existing online menu layout. The implementer should check `render.c` for how the online menu is currently laid out and position the friend panel so it doesn't overlap existing UI.

- [ ] **Step 5: Build all and verify**

Run: `make debug-all`
Expected: Clean build, no errors.

- [ ] **Step 6: Commit**

```bash
git add src/game/online_ui.h src/game/online_ui.c src/render/render.c
git commit -m "feat(ui): integrate friend panel into online menu — init, update, render"
```

---

### Task 10: Room Invite Flow — Accept via Friend Panel

**Files:**
- Modify: `src/render/friend_panel_render.c` (Accept button handler for invites)
- Modify: `src/game/online_ui.c` (transition to join flow on invite accept)

- [ ] **Step 1: Handle invite acceptance in friend panel**

In `friend_panel_render_input`, when the Accept button on a `FRIEND_ENTRY_INVITE` is clicked:

```c
    /* On Accept click for room invite: */
    lobby_client_join_room(entry->room_code);
    /* Remove the invite entry from the list */
    for (int j = i; j < state->entry_count - 1; j++)
        state->entries[j] = state->entries[j + 1];
    state->entry_count--;
```

- [ ] **Step 2: Ensure online_ui handles the resulting room assignment**

The existing `lobby_client_join_room` flow already triggers `ROOM_ASSIGNED` → `ONLINE_SUB_CONNECTING` transition. Verify this path works by checking that `online_ui.c` handles `lobby_client_has_room_assignment()` regardless of current subphase (it should work from `ONLINE_SUB_MENU` which is where an invite recipient would be).

The implementer should read `online_ui.c` and confirm the room assignment polling happens in all relevant subphases. If it only runs in `ONLINE_SUB_JOIN_WAITING`, extend it to also run in `ONLINE_SUB_MENU`.

- [ ] **Step 3: Build and verify**

Run: `make debug-all`
Expected: Clean build, no errors.

- [ ] **Step 4: Commit**

```bash
git add src/render/friend_panel_render.c src/game/online_ui.c
git commit -m "feat(ui): wire room invite acceptance — join room via friend panel"
```

---

### Task 11: Room Invite Expiration — Server-Side Hooks

**Files:**
- Modify: `src/lobby/lobby_net.c` (call `friends_expire_room_invites` on room start/fill)

- [ ] **Step 1: Expire invites when room starts a game**

In `lby_handle_server_room_created` (or wherever the lobby transitions a room from "waiting" to "playing"), call:

```c
    friends_expire_room_invites(room_code);
```

This ensures that once a game starts, any pending invites for that room are cleaned up and the invitee sees the invite disappear.

- [ ] **Step 2: Verify dead server cleanup calls room_destroyed**

In `lby_on_dead_server`, verify that it already calls `lby_handle_server_room_destroyed` for each room on that server (or equivalent cleanup). If it does, the `friends_on_room_destroyed` + `friends_expire_room_invites` calls from Task 5 Step 5 will fire automatically.

If `lby_on_dead_server` doesn't currently iterate rooms, the implementer needs to add that logic — query `room_codes` table for rooms on the dead server's addr:port, then call `friends_on_room_destroyed` and `friends_expire_room_invites` for each.

- [ ] **Step 3: Build and verify**

Run: `make lobby`
Expected: Clean build, no errors.

- [ ] **Step 4: Commit**

```bash
git add src/lobby/lobby_net.c
git commit -m "feat(lobby): expire room invites on game start and dead server cleanup"
```

---

### Task 12: Add Username Lookup Prepared Statement

**Files:**
- Modify: `src/lobby/db.h` (add `LOBBY_STMT_GET_USERNAME`)
- Modify: `src/lobby/db.c` (add SQL)

The `friends.c` helper `get_username(db, account_id, out)` needs a statement to look up username by id. This wasn't in the existing prepared statements.

- [ ] **Step 1: Add statement ID**

In `src/lobby/db.h`, add before `LOBBY_STMT__COUNT`:

```c
    LOBBY_STMT_GET_USERNAME,
```

- [ ] **Step 2: Add SQL**

In `src/lobby/db.c`, add to `STMT_SQL[]`:

```c
    [LOBBY_STMT_GET_USERNAME] =
        "SELECT username FROM accounts WHERE id = ?",
```

- [ ] **Step 3: Implement get_username in friends.c**

```c
static bool get_username(LobbyDB *db, int32_t account_id, char *out_username)
{
    sqlite3_stmt *stmt = lobbydb_stmt(db, LOBBY_STMT_GET_USERNAME);
    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, account_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        strncpy(out_username, name, 31);
        out_username[31] = '\0';
        return true;
    }
    out_username[0] = '\0';
    return false;
}
```

- [ ] **Step 4: Build and verify**

Run: `make lobby`
Expected: Clean build, no errors.

- [ ] **Step 5: Commit**

```bash
git add src/lobby/db.h src/lobby/db.c src/lobby/friends.c
git commit -m "feat(db): add GET_USERNAME prepared statement for friend username lookups"
```

---

### Task 13: Final Integration Testing and Polish

**Files:**
- All modified files from previous tasks

- [ ] **Step 1: Build all three binaries**

Run: `make debug-all`
Expected: Clean build, zero warnings, zero errors.

- [ ] **Step 2: Check LSP diagnostics**

Use LSP `getDiagnostics` on all new and modified files to catch type errors, missing includes, or unused variables.

- [ ] **Step 3: Verify room invite struct has room_code**

The room invite flow in Task 4 Step 10 noted that `NetMsgRoomInvite` may need a `room_code[8]` field so the client can tell the lobby which room to invite into. If the implementer hasn't addressed this, add the field now:

In `protocol.h`, update `NetMsgRoomInvite`:
```c
typedef struct NetMsgRoomInvite {
    uint8_t auth_token[32];
    int32_t target_account_id;
    char    room_code[8];       /* which room to invite into */
} NetMsgRoomInvite;
```

Update serialize/deserialize in `protocol.c` to include the extra 8 bytes (total 44).

Update `lobby_client_room_invite` to accept and fill `room_code`.

- [ ] **Step 4: Run @code-auditor**

Run the code auditor agent on all new files for correctness, memory safety, and C best practices.

- [ ] **Step 5: Run @dependency-mapper**

Update `.claude/deps.json` since we added new headers, structs, enums, and function signatures.

- [ ] **Step 6: Final commit**

```bash
git add -A
git commit -m "feat: friendlist system — complete integration and polish"
```
