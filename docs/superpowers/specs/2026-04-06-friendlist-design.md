# Friendlist System Design

## Overview

A friend system for Hollow Hearts that lets players search for users, send/accept/reject friend requests, see online presence, invite friends to rooms, and remove friends. All logic is lobby-centric with SQLite persistence.

## Constraints

- Max 20 friends per player
- Max 10 outgoing pending friend requests
- Rejection permanently blocks re-requesting (visible as "Blocked")
- Username search: prefix match, minimum 4 characters, max 10 results
- Search rate-limited to 1 request per second per connection
- Symmetric friend removal (one click removes both sides)
- Room invites only to green-dot (lobby-connected) friends

## Database Schema

Migration version 8. Three new tables.

### friendships

Confirmed mutual friendships. Canonical ordering (`account_a < account_b`) ensures each friendship stored once.

```sql
CREATE TABLE friendships (
  account_a  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  account_b  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
  PRIMARY KEY (account_a, account_b),
  CHECK(account_a < account_b)
);
CREATE INDEX idx_friendships_b ON friendships(account_b);
```

### friend_requests

Pending unconfirmed requests.

```sql
CREATE TABLE friend_requests (
  from_account INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  to_account   INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  created_at   INTEGER NOT NULL DEFAULT (strftime('%s','now')),
  PRIMARY KEY (from_account, to_account)
);
CREATE INDEX idx_friend_requests_to ON friend_requests(to_account);
```

### friend_blocks

Permanent rejection records. Prevents re-requesting.

```sql
CREATE TABLE friend_blocks (
  blocker_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  blocked_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
  PRIMARY KEY (blocker_id, blocked_id)
);
```

## Protocol Messages

### Client → Lobby

| Message | Struct Fields | Purpose |
|---------|--------------|---------|
| `NET_MSG_FRIEND_SEARCH` | `auth_token[32], query[32]` | Username prefix search |
| `NET_MSG_FRIEND_REQUEST` | `auth_token[32], target_account_id` | Send friend request |
| `NET_MSG_FRIEND_ACCEPT` | `auth_token[32], from_account_id` | Accept pending request |
| `NET_MSG_FRIEND_REJECT` | `auth_token[32], from_account_id` | Reject pending request (creates block) |
| `NET_MSG_FRIEND_REMOVE` | `auth_token[32], target_account_id` | Remove friend (symmetric) |
| `NET_MSG_FRIEND_LIST_REQUEST` | `auth_token[32]` | Request full friend list (sent on login) |
| `NET_MSG_ROOM_INVITE` | `auth_token[32], target_account_id` | Invite friend to current room |

### Lobby → Client

| Message | Key Fields | Purpose |
|---------|-----------|---------|
| `NET_MSG_FRIEND_SEARCH_RESULT` | `count, results[10]{username, account_id, status}` | Search results with relationship status |
| `NET_MSG_FRIEND_LIST` | `friend_count, friends[20]{username, account_id, presence}, request_count, incoming_requests[10]{username, account_id}` | Full friend list + pending incoming requests |
| `NET_MSG_FRIEND_UPDATE` | `account_id, presence` | Real-time single-friend presence change |
| `NET_MSG_FRIEND_REQUEST_NOTIFY` | `username, account_id` | Pushed when someone sends you a request |
| `NET_MSG_ROOM_INVITE_NOTIFY` | `from_username, room_code` | Pushed when a friend invites you to a room |
| `NET_MSG_ROOM_INVITE_EXPIRED` | `room_code` | Room invite no longer valid |

### Search result status values

- `0` = available (can add)
- `1` = already friends
- `2` = pending (you sent a request)
- `3` = pending (they sent you a request)
- `4` = blocked (they rejected you)
- `5` = self

### Presence values

- `0` = offline
- `1` = online (green dot — connected to lobby)
- `2` = in-game (yellow dot — in active room)

## Lobby Server Logic

### New file: `src/lobby/friends.c/h`

All friend DB queries and handler logic. Called from `lobby_net.c` message dispatch.

### Search

Query `accounts` with `username LIKE 'query%' COLLATE NOCASE LIMIT 10`. Cross-reference against `friendships`, `friend_requests`, `friend_blocks` to set status per result. Exclude self.

### Send request

Validations:
1. Not self
2. Not already friends
3. No existing pending request in either direction (if target already sent one to sender, auto-accept instead)
4. Not blocked by target
5. Sender has < 10 outgoing pending requests
6. Target has < 20 friends

Insert into `friend_requests`. If target is online, push `FRIEND_REQUEST_NOTIFY`.

### Accept request

Delete from `friend_requests`. Insert into `friendships` (canonical order — smaller id first). Both players' friend counts must be checked (≤20). If sender is online, push `FRIEND_UPDATE` with current presence.

### Reject request

Delete from `friend_requests`. Insert into `friend_blocks(blocker=rejecter, blocked=requester)`.

### Remove friend

Delete from `friendships`. If the other player is online, push `FRIEND_UPDATE` to signal removal. Clean up any pending room invites involving this pair.

### Friend list on login

On authentication, query all friendships + incoming pending requests. For each friend, determine presence:
- Check if account_id has active lobby connection → online
- Check if account_id is in the in-game presence map → in_game
- Otherwise → offline

Send `NET_MSG_FRIEND_LIST`. Also push `FRIEND_UPDATE(presence=online)` to each of this player's online friends.

### Presence tracking

**Online/offline:** On authentication, notify friends. On disconnect, push `FRIEND_UPDATE(presence=offline)` to each online friend.

**In-game:** Maintain an in-memory lookup `account_id → room_code` for active game players. Populated from `SERVER_ROOM_CREATED` (which includes `player_tokens` mappable to account_ids). Cleared on `SERVER_ROOM_DESTROYED` and `lby_on_dead_server`. On state changes, push `FRIEND_UPDATE` to affected players' online friends.

### Room invites

- Track pending invites in memory (ephemeral, not DB).
- On `ROOM_INVITE`: verify sender has an active room, target is online (green dot), target is a friend. Send `ROOM_INVITE_NOTIFY`.
- On room start/fill/destroy: send `ROOM_INVITE_EXPIRED` to pending invitees.
- On accept: lobby checks room is still joinable, then sends `ROOM_ASSIGNED` as if the player had joined via room code. If room is no longer valid, send error.

### Rate limiting

Track last search timestamp per connection in `LobbyConnInfo`. Reject searches more frequent than 1/second.

## Client Architecture

### New file: `src/game/friend_panel.c/h`

Friend panel UI logic — state management, input handling, action dispatch.

### New file: `src/render/friend_panel.c/h`

Friend panel rendering — drawing, layout, scrolling, hit-testing.

### State

```c
typedef enum {
    FRIEND_ENTRY_FRIEND,
    FRIEND_ENTRY_REQUEST,
    FRIEND_ENTRY_INVITE,
} FriendEntryType;

typedef struct {
    FriendEntryType type;
    char username[32];
    int32_t account_id;
    uint8_t presence;       // friends only: 0=offline, 1=online, 2=in_game
    char room_code[8];      // room invites only
} FriendEntry;

typedef struct {
    FriendEntry entries[32]; // 20 friends + 10 requests + room invites
    int entry_count;

    char search_buf[32];
    int search_len;
    bool search_active;
    struct { char username[32]; int32_t account_id; uint8_t status; } search_results[10];
    int search_result_count;

    float scroll_offset;
    float scroll_target;

    bool context_menu_open;
    int context_menu_entry;

    bool confirm_remove_open;
    int confirm_remove_entry;

    bool can_invite; // true when in room lobby
} FriendPanelState;
```

### Panel ordering (top to bottom)

1. Room invites — labeled "Room Invitation", Accept/Reject buttons
2. Friend requests — labeled "Friend Request", Accept/Reject buttons
3. Online friends — green dot, Invite button when `can_invite`
4. In-game friends — yellow dot
5. Offline friends — no dot

### Search bar

Below the friend panel. Typing ≥4 chars sends `NET_MSG_FRIEND_SEARCH`. Results roll out upward, each with contextual button: "Add" / "Pending" / "Blocked" (grey) / "Friends".

### Context menu

Right-click on a friend → "Remove Friend". Opens confirmation dialog: "Remove [username]?" with Yes/No.

### Integration with lobby_client

New functions in `lobby_client.c/h`:
- `lobby_client_friend_search(query)`
- `lobby_client_friend_request(account_id)`
- `lobby_client_friend_accept(account_id)`
- `lobby_client_friend_reject(account_id)`
- `lobby_client_friend_remove(account_id)`
- `lobby_client_room_invite(account_id)`
- Polling/callback functions for receiving pushed updates

### Visibility

Friend panel visible only when authenticated and in the online menu area. Persists across online subphases. `can_invite = true` when in `ONLINE_SUB_CONNECTED_WAITING`.

## Edge Cases and Risks

### Race conditions

- **Simultaneous friend requests:** If A requests B while B requests A, lobby detects existing reverse request and auto-accepts instead of creating duplicate.
- **Remove during invite:** Remove handler cleans up pending invites for the pair.
- **Room fills between invite and accept:** Lobby checks room status before sending `ROOM_ASSIGNED`. If invalid, sends error. `ROOM_INVITE_EXPIRED` should have already fired but network ordering isn't guaranteed, so the fallback check is necessary.

### Presence edge cases

- **Brief offline gap:** Player disconnects from lobby → joins game server → lobby receives `SERVER_ROOM_CREATED`. Player appears offline for a few seconds. Acceptable.
- **Lobby crash/restart:** In-memory presence lost. Friends show offline until reconnection or room lifecycle events. DB-backed friend list survives.
- **Game server crash:** `SERVER_ROOM_DESTROYED` never arrives. Players stuck yellow until `lby_on_dead_server` cleanup fires, which should also clear the in-game presence map.

### Abuse prevention

- **Search spam:** Rate-limited to 1/second via timestamp in `LobbyConnInfo`.
- **Request spam:** Capped at 10 outgoing pending.
- **Permanent blocking on rejection:** Prevents harassment loops.
- **Blocked visibility:** Blocked player sees "Blocked" button — intentional design choice.

### Data integrity

- **Account deletion:** All tables use `ON DELETE CASCADE`.
- **20-friend cap:** Check both sides of `friendships` table atomically with insert (transaction).
- **Canonical friendship ordering:** `CHECK(account_a < account_b)` prevents duplicate entries.

### Room invite reliability

- Invites are ephemeral (in-memory only). Lobby restart loses pending invites — acceptable since rooms would also be disrupted.
- Auto-expire on room start, room full, room destroy, or friend removal.
