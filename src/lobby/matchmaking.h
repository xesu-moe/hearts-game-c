/* ============================================================
 * Lobby Server — Matchmaking Queue
 *
 * FIFO queue that groups 4 players into a match. When a match
 * forms, the lobby creates a room on a game server and sends
 * all 4 players the server address.
 *
 * This module is a pure data structure — it does not access
 * the network layer or send messages. lobby_net.c calls into
 * this module and handles all messaging.
 *
 * @deps-exports: MmQueueEntry, MmPendingMatch, MmMatchResult,
 *                MmPendingTimeoutCb, mm_init, mm_queue_add,
 *                mm_queue_remove, mm_try_form_match,
 *                mm_queue_count, mm_pending_add,
 *                mm_pending_find_by_code, mm_pending_get,
 *                mm_pending_remove, mm_pending_remove_by_conn,
 *                mm_pending_remove_by_server, mm_pending_expire
 * @deps-requires: net/protocol.h (NET_ROOM_CODE_LEN)
 * @deps-used-by: lobby/lobby_net.c
 * @deps-last-changed: 2026-03-25 — Step 17: Matchmaking Queue
 * ============================================================ */

#ifndef LOBBY_MATCHMAKING_H
#define LOBBY_MATCHMAKING_H

#include <stdbool.h>
#include <stdint.h>

#include "net/protocol.h"

/* ================================================================
 * Constants
 * ================================================================ */

#define MM_MAX_QUEUE         1024
#define MM_MAX_PENDING       16
#define MM_PLAYERS_PER_MATCH 4
#define MM_PENDING_TIMEOUT   15.0 /* seconds to wait for server ACK */

/* ================================================================
 * Queue Entry
 * ================================================================ */

typedef struct MmQueueEntry {
    bool     active;
    int      conn_id;
    int32_t  account_id;
    uint64_t enqueue_order; /* monotonic counter for FIFO ordering */
} MmQueueEntry;

/* ================================================================
 * Pending Match (waiting for game server ACK)
 * ================================================================ */

typedef struct MmPendingMatch {
    bool    active;
    char    room_code[NET_ROOM_CODE_LEN];
    int     server_conn_id;
    int     conn_ids[MM_PLAYERS_PER_MATCH];
    int32_t account_ids[MM_PLAYERS_PER_MATCH];
    double  created_at;
} MmPendingMatch;

/* ================================================================
 * Match Result (returned by mm_try_form_match)
 * ================================================================ */

typedef struct MmMatchResult {
    bool    formed;
    int     conn_ids[MM_PLAYERS_PER_MATCH];
    int32_t account_ids[MM_PLAYERS_PER_MATCH];
} MmMatchResult;

/* ================================================================
 * Queue API
 * ================================================================ */

/* Initialize the matchmaking system. Call once at lobby startup. */
void mm_init(void);

/* Add a player to the queue.
 * Returns queue position (1-based) on success, or -1 on error
 * (queue full or duplicate account_id). */
int mm_queue_add(int conn_id, int32_t account_id);

/* Remove a player from the queue by connection ID.
 * Returns true if the player was found and removed. */
bool mm_queue_remove(int conn_id);

/* Check if a match can be formed (4+ players in queue).
 * If yes, removes the 4 oldest entries and returns them.
 * If no, returns {.formed = false}. */
MmMatchResult mm_try_form_match(void);

/* Return the number of players currently in the queue. */
int mm_queue_count(void);

/* ================================================================
 * Pending Match API
 * ================================================================ */

/* Track a pending matchmade room (waiting for server ACK).
 * Returns index (0..MM_MAX_PENDING-1) on success, or -1 if full. */
int mm_pending_add(const char *code, int server_conn,
                   const int conn_ids[MM_PLAYERS_PER_MATCH],
                   const int32_t account_ids[MM_PLAYERS_PER_MATCH],
                   double now);

/* Find a pending match by room code.
 * Returns index or -1 if not found. */
int mm_pending_find_by_code(const char *code);

/* Get a pending match by index. Returns NULL if inactive. */
const MmPendingMatch *mm_pending_get(int idx);

/* Remove a pending match by index. */
void mm_pending_remove(int idx);

/* Remove all pending matches involving a given client conn_id.
 * If a match is removed and out_match is non-NULL, copies the removed
 * match data into *out_match and returns true. Returns false if no
 * pending match was found for this conn_id. */
bool mm_pending_remove_by_conn(int conn_id, MmPendingMatch *out_match);

/* Remove all pending matches involving a given server conn_id. */
void mm_pending_remove_by_server(int conn_id);

/* Expire stale pending matches older than timeout seconds.
 * Calls on_timeout for each expired entry with the 4 conn_ids. */
typedef void (*MmPendingTimeoutCb)(const int conn_ids[MM_PLAYERS_PER_MATCH]);
void mm_pending_expire(double now, double timeout,
                       MmPendingTimeoutCb on_timeout);

#endif /* LOBBY_MATCHMAKING_H */
