#ifndef FRIEND_PANEL_H
#define FRIEND_PANEL_H

#include "../net/protocol.h"
#include <stdbool.h>
#include <stdint.h>

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
    char    current_room_code[8]; /* set by online_ui when in a room */
} FriendPanelState;

void friend_panel_init(FriendPanelState *state);
void friend_panel_update(FriendPanelState *state, float dt);
void friend_panel_set_can_invite(FriendPanelState *state, bool can_invite);

#endif /* FRIEND_PANEL_H */
