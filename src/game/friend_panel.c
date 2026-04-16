/* ============================================================
 * Client — Friend Panel State Management
 *
 * Manages friend list, search, invites, and presence state.
 * Polls lobby_client friend buffers each frame and applies updates.
 *
 * @deps-exports: FriendPanelState, FriendEntry, FriendEntryType,
 *                friend_panel_init, friend_panel_update,
 *                friend_panel_set_can_invite
 * @deps-requires: game/friend_panel.h,
 *                 net/lobby_client.h (lobby_client_has_friend_list,
 *                   lobby_client_has_friend_search_result,
 *                   lobby_client_has_friend_update,
 *                   lobby_client_has_friend_request_notify,
 *                   lobby_client_has_room_invite_notify,
 *                   lobby_client_has_room_invite_expired,
 *                   lobby_client_friend_list_request),
 *                 net/protocol.h (NetMsgFriendList, NetMsgFriendSearchResult,
 *                   NetMsgFriendUpdate, NetMsgFriendRequestNotify,
 *                   NetMsgRoomInviteNotify, NetMsgRoomInviteExpired,
 *                   FRIEND_PRESENCE_ONLINE, FRIEND_PRESENCE_IN_GAME)
 * @deps-used-by: game/online_ui.c
 * @deps-last-changed: 2026-04-06 — Initial implementation
 * ============================================================ */

#include "friend_panel.h"
#include "../net/lobby_client.h"
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * Internal Helpers
 * ================================================================ */

static int entry_sort_key(const FriendEntry *e) {
    if (e->type == FRIEND_ENTRY_INVITE)  return 0;
    if (e->type == FRIEND_ENTRY_REQUEST) return 1;
    if (e->type == FRIEND_ENTRY_FRIEND) {
        if (e->presence == FRIEND_PRESENCE_ONLINE)  return 2;
        if (e->presence == FRIEND_PRESENCE_IN_GAME) return 3;
        return 4; /* offline */
    }
    return 5;
}

static int entry_cmp(const void *a, const void *b) {
    return entry_sort_key((const FriendEntry *)a)
         - entry_sort_key((const FriendEntry *)b);
}

static void sort_entries(FriendPanelState *state) {
    qsort(state->entries, (size_t)state->entry_count,
          sizeof(FriendEntry), entry_cmp);
}

/* ================================================================
 * Network Update Applicators
 * ================================================================ */

static void apply_friend_list(FriendPanelState *state,
                               const NetMsgFriendList *fl) {
    /* Preserve existing invite entries */
    FriendEntry saved_invites[32];
    int         invite_count = 0;

    for (int i = 0; i < state->entry_count && invite_count < 32; i++) {
        if (state->entries[i].type == FRIEND_ENTRY_INVITE) {
            saved_invites[invite_count++] = state->entries[i];
        }
    }

    state->entry_count = 0;

    /* Re-add saved invites */
    for (int i = 0; i < invite_count && state->entry_count < 32; i++) {
        state->entries[state->entry_count++] = saved_invites[i];
    }

    /* Add incoming friend requests */
    for (int i = 0; i < (int)fl->request_count && state->entry_count < 32; i++) {
        FriendEntry e;
        memset(&e, 0, sizeof(e));
        e.type       = FRIEND_ENTRY_REQUEST;
        e.account_id = fl->incoming_requests[i].account_id;
        memcpy(e.username, fl->incoming_requests[i].username,
               sizeof(e.username) - 1);
        state->entries[state->entry_count++] = e;
    }

    /* Add confirmed friends */
    for (int i = 0; i < (int)fl->friend_count && state->entry_count < 32; i++) {
        FriendEntry e;
        memset(&e, 0, sizeof(e));
        e.type       = FRIEND_ENTRY_FRIEND;
        e.account_id = fl->friends[i].account_id;
        e.presence   = fl->friends[i].presence;
        memcpy(e.username, fl->friends[i].username, sizeof(e.username) - 1);
        state->entries[state->entry_count++] = e;
    }

    sort_entries(state);
}

static void apply_friend_update(FriendPanelState *state,
                                const NetMsgFriendUpdate *upd) {
    if (upd->presence == 0xFF) {
        /* Remove the friend entry */
        for (int i = 0; i < state->entry_count; i++) {
            if (state->entries[i].type == FRIEND_ENTRY_FRIEND &&
                state->entries[i].account_id == upd->account_id) {
                /* Shift remaining entries down */
                memmove(&state->entries[i], &state->entries[i + 1],
                        (size_t)(state->entry_count - i - 1) * sizeof(FriendEntry));
                state->entry_count--;
                return;
            }
        }
        return;
    }

    /* Update presence for existing friend entry */
    for (int i = 0; i < state->entry_count; i++) {
        if (state->entries[i].type == FRIEND_ENTRY_FRIEND &&
            state->entries[i].account_id == upd->account_id) {
            state->entries[i].presence = upd->presence;
            sort_entries(state);
            return;
        }
    }

    /* Friend not found — newly accepted; request a full list refresh */
    lobby_client_friend_list_request();
}

static void apply_request_notify(FriendPanelState *state,
                                 const NetMsgFriendRequestNotify *rn) {
    if (state->entry_count >= 32) return;

    FriendEntry e;
    memset(&e, 0, sizeof(e));
    e.type       = FRIEND_ENTRY_REQUEST;
    e.account_id = rn->account_id;
    memcpy(e.username, rn->username, sizeof(e.username) - 1);
    state->entries[state->entry_count++] = e;

    sort_entries(state);
}

static void apply_room_invite(FriendPanelState *state,
                              const NetMsgRoomInviteNotify *inv) {
    if (state->entry_count >= 32) return;

    FriendEntry e;
    memset(&e, 0, sizeof(e));
    e.type = FRIEND_ENTRY_INVITE;
    memcpy(e.username,  inv->from_username, sizeof(e.username) - 1);
    memcpy(e.room_code, inv->room_code,     sizeof(e.room_code) - 1);
    state->entries[state->entry_count++] = e;

    sort_entries(state);
}

static void apply_room_invite_expired(FriendPanelState *state,
                                      const NetMsgRoomInviteExpired *exp) {
    for (int i = 0; i < state->entry_count; i++) {
        if (state->entries[i].type == FRIEND_ENTRY_INVITE &&
            strncmp(state->entries[i].room_code, exp->room_code,
                    sizeof(exp->room_code)) == 0) {
            memmove(&state->entries[i], &state->entries[i + 1],
                    (size_t)(state->entry_count - i - 1) * sizeof(FriendEntry));
            state->entry_count--;
            return;
        }
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

void friend_panel_init(FriendPanelState *state) {
    memset(state, 0, sizeof(*state));
}

void friend_panel_update(FriendPanelState *state, float dt) {
    (void)dt;

    /* Poll friend list */
    {
        NetMsgFriendList fl;
        if (lobby_client_has_friend_list(&fl)) {
            apply_friend_list(state, &fl);
        }
    }

    /* Poll friend search results — discard if search text dropped below 4 chars */
    {
        NetMsgFriendSearchResult sr;
        if (lobby_client_has_friend_search_result(&sr)) {
            if (state->search_len >= 4) {
                int count = (sr.count < 10) ? (int)sr.count : 10;
                state->search_result_count = count;
                for (int i = 0; i < count; i++) {
                    memset(&state->search_results[i], 0,
                           sizeof(state->search_results[i]));
                    memcpy(state->search_results[i].username,
                           sr.results[i].username,
                           sizeof(state->search_results[i].username) - 1);
                    state->search_results[i].account_id = sr.results[i].account_id;
                    state->search_results[i].status     = sr.results[i].status;
                }
                state->search_results_visible = true;
            }
        }
    }

    /* Drain friend updates */
    {
        NetMsgFriendUpdate upd;
        while (lobby_client_has_friend_update(&upd)) {
            apply_friend_update(state, &upd);
        }
    }

    /* Drain friend request notifications */
    {
        NetMsgFriendRequestNotify rn;
        while (lobby_client_has_friend_request_notify(&rn)) {
            apply_request_notify(state, &rn);
        }
    }

    /* Poll room invite notify */
    {
        NetMsgRoomInviteNotify inv;
        if (lobby_client_has_room_invite_notify(&inv)) {
            apply_room_invite(state, &inv);
        }
    }

    /* Poll room invite expired */
    {
        NetMsgRoomInviteExpired exp;
        if (lobby_client_has_room_invite_expired(&exp)) {
            apply_room_invite_expired(state, &exp);
        }
    }

    /* Smooth scroll */
    state->scroll_offset +=
        (state->scroll_target - state->scroll_offset) * 0.15f;
}

void friend_panel_set_can_invite(FriendPanelState *state, bool can_invite) {
    state->can_invite = can_invite;
}
