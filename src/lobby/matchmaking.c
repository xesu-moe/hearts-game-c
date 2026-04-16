/* ============================================================
 * @deps-implements: lobby/matchmaking.h
 * @deps-requires: lobby/matchmaking.h (MmQueueEntry, MmPendingMatch,
 *                 MmMatchResult, MM_MAX_QUEUE, MM_MAX_PENDING,
 *                 MM_PLAYERS_PER_MATCH, MmPendingTimeoutCb),
 *                 net/protocol.h (NET_ROOM_CODE_LEN),
 *                 stdio.h, string.h
 * @deps-last-changed: 2026-03-25 — Step 17: Matchmaking Queue
 * ============================================================ */

#include "matchmaking.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
 * File-scope state
 * ================================================================ */

static MmQueueEntry   g_queue[MM_MAX_QUEUE];
static MmPendingMatch g_pending[MM_MAX_PENDING];
static uint64_t       g_enqueue_counter;

/* ================================================================
 * Queue API
 * ================================================================ */

void mm_init(void)
{
    memset(g_queue, 0, sizeof(g_queue));
    memset(g_pending, 0, sizeof(g_pending));
    g_enqueue_counter = 0;
    printf("[matchmaking] Initialized (max queue=%d, max pending=%d)\n",
           MM_MAX_QUEUE, MM_MAX_PENDING);
}

int mm_queue_add(int conn_id, int32_t account_id)
{
    /* Check for duplicate account_id */
    for (int i = 0; i < MM_MAX_QUEUE; i++) {
        if (g_queue[i].active && g_queue[i].account_id == account_id) {
            printf("[matchmaking] Duplicate queue entry for account %d\n",
                   account_id);
            return -1;
        }
    }

    /* Find first inactive slot */
    int slot = -1;
    for (int i = 0; i < MM_MAX_QUEUE; i++) {
        if (!g_queue[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        printf("[matchmaking] Queue full (%d entries)\n", MM_MAX_QUEUE);
        return -1;
    }

    g_queue[slot].active = true;
    g_queue[slot].conn_id = conn_id;
    g_queue[slot].account_id = account_id;
    g_queue[slot].enqueue_order = g_enqueue_counter++;

    /* Calculate position: count active entries with lower enqueue_order */
    int position = 1;
    for (int i = 0; i < MM_MAX_QUEUE; i++) {
        if (g_queue[i].active && i != slot &&
            g_queue[i].enqueue_order < g_queue[slot].enqueue_order) {
            position++;
        }
    }

    printf("[matchmaking] Added conn %d (account %d) at position %d "
           "(total queued: %d)\n",
           conn_id, account_id, position, mm_queue_count());
    return position;
}

bool mm_queue_remove(int conn_id)
{
    for (int i = 0; i < MM_MAX_QUEUE; i++) {
        if (g_queue[i].active && g_queue[i].conn_id == conn_id) {
            g_queue[i].active = false;
            printf("[matchmaking] Removed conn %d from queue\n", conn_id);
            return true;
        }
    }
    return false;
}

MmMatchResult mm_try_form_match(void)
{
    MmMatchResult result;
    memset(&result, 0, sizeof(result));

    /* Count active entries */
    int count = 0;
    for (int i = 0; i < MM_MAX_QUEUE; i++) {
        if (g_queue[i].active) count++;
    }
    if (count < MM_PLAYERS_PER_MATCH) {
        return result;
    }

    /* Find the 4 entries with lowest enqueue_order */
    int picked[MM_PLAYERS_PER_MATCH];
    for (int p = 0; p < MM_PLAYERS_PER_MATCH; p++) {
        int best = -1;
        uint64_t best_order = UINT64_MAX;
        for (int i = 0; i < MM_MAX_QUEUE; i++) {
            if (!g_queue[i].active) continue;
            if (g_queue[i].enqueue_order >= best_order) continue;
            /* Check this slot wasn't already picked */
            bool already = false;
            for (int j = 0; j < p; j++) {
                if (picked[j] == i) { already = true; break; }
            }
            if (already) continue;
            best = i;
            best_order = g_queue[i].enqueue_order;
        }
        picked[p] = best;
    }

    /* Dequeue and fill result */
    result.formed = true;
    for (int p = 0; p < MM_PLAYERS_PER_MATCH; p++) {
        int idx = picked[p];
        result.conn_ids[p] = g_queue[idx].conn_id;
        result.account_ids[p] = g_queue[idx].account_id;
        g_queue[idx].active = false;
    }

    printf("[matchmaking] Match formed: conns [%d, %d, %d, %d]\n",
           result.conn_ids[0], result.conn_ids[1],
           result.conn_ids[2], result.conn_ids[3]);
    return result;
}

int mm_queue_count(void)
{
    int count = 0;
    for (int i = 0; i < MM_MAX_QUEUE; i++) {
        if (g_queue[i].active) count++;
    }
    return count;
}

/* ================================================================
 * Pending Match API
 * ================================================================ */

int mm_pending_add(const char *code, int server_conn,
                   const int conn_ids[MM_PLAYERS_PER_MATCH],
                   const int32_t account_ids[MM_PLAYERS_PER_MATCH],
                   double now)
{
    for (int i = 0; i < MM_MAX_PENDING; i++) {
        if (!g_pending[i].active) {
            g_pending[i].active = true;
            strncpy(g_pending[i].room_code, code, NET_ROOM_CODE_LEN - 1);
            g_pending[i].room_code[NET_ROOM_CODE_LEN - 1] = '\0';
            g_pending[i].server_conn_id = server_conn;
            memcpy(g_pending[i].conn_ids, conn_ids,
                   sizeof(int) * MM_PLAYERS_PER_MATCH);
            memcpy(g_pending[i].account_ids, account_ids,
                   sizeof(int32_t) * MM_PLAYERS_PER_MATCH);
            g_pending[i].created_at = now;
            printf("[matchmaking] Pending match added: code='%.8s', idx=%d\n",
                   code, i);
            return i;
        }
    }
    printf("[matchmaking] Pending match array full (%d)\n", MM_MAX_PENDING);
    return -1;
}

int mm_pending_find_by_code(const char *code)
{
    for (int i = 0; i < MM_MAX_PENDING; i++) {
        if (g_pending[i].active &&
            strncmp(g_pending[i].room_code, code, NET_ROOM_CODE_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

const MmPendingMatch *mm_pending_get(int idx)
{
    if (idx < 0 || idx >= MM_MAX_PENDING) return NULL;
    if (!g_pending[idx].active) return NULL;
    return &g_pending[idx];
}

void mm_pending_remove(int idx)
{
    if (idx >= 0 && idx < MM_MAX_PENDING) {
        g_pending[idx].active = false;
    }
}

bool mm_pending_remove_by_conn(int conn_id, MmPendingMatch *out_match)
{
    for (int i = 0; i < MM_MAX_PENDING; i++) {
        if (!g_pending[i].active) continue;
        for (int p = 0; p < MM_PLAYERS_PER_MATCH; p++) {
            if (g_pending[i].conn_ids[p] == conn_id) {
                printf("[matchmaking] Removing pending match idx=%d "
                       "(client conn %d disconnected)\n", i, conn_id);
                if (out_match) {
                    *out_match = g_pending[i];
                }
                g_pending[i].active = false;
                return true;
            }
        }
    }
    return false;
}

void mm_pending_remove_by_server(int conn_id)
{
    for (int i = 0; i < MM_MAX_PENDING; i++) {
        if (g_pending[i].active && g_pending[i].server_conn_id == conn_id) {
            printf("[matchmaking] Removing pending match idx=%d "
                   "(server conn %d disconnected)\n", i, conn_id);
            g_pending[i].active = false;
        }
    }
}

void mm_pending_expire(double now, double timeout,
                       MmPendingTimeoutCb on_timeout)
{
    for (int i = 0; i < MM_MAX_PENDING; i++) {
        if (!g_pending[i].active) continue;
        if (now - g_pending[i].created_at > timeout) {
            printf("[matchmaking] Pending match idx=%d expired (code='%.8s')\n",
                   i, g_pending[i].room_code);
            if (on_timeout) {
                on_timeout(g_pending[i].conn_ids);
            }
            g_pending[i].active = false;
        }
    }
}
