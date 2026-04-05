/* ============================================================
 * Online Menu UI — State and Room Code Input
 *
 * Manages the online submenu state: Create Room, Join Room,
 * Quick Match, waiting room, and game server connection flow.
 *
 * @deps-exports: OnlineSubphase, OnlineUIState (with has_reconnect field),
 *                online_ui_init, online_ui_update_text_input
 * @deps-requires: net/protocol.h (NET_ROOM_CODE_LEN, NET_ADDR_LEN,
 *                 NET_AUTH_TOKEN_LEN, NET_MAX_PLAYERS, NET_MAX_NAME_LEN)
 * @deps-used-by: main.c, game/update.c, render/render.c
 * @deps-last-changed: 2026-03-31 — Added has_reconnect bool field to OnlineUIState
 * ============================================================ */

#ifndef ONLINE_UI_H
#define ONLINE_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "net/protocol.h"

typedef enum OnlineSubphase {
    ONLINE_SUB_MENU = 0,        /* Create / Join / Quick Match / Back */
    ONLINE_SUB_CREATE_WAITING,  /* Room created, showing code + player slots */
    ONLINE_SUB_JOIN_INPUT,      /* Text input for room code */
    ONLINE_SUB_JOIN_WAITING,    /* Room code submitted, waiting for ROOM_ASSIGNED */
    ONLINE_SUB_QUEUE_SEARCHING, /* Quick Match searching... */
    ONLINE_SUB_MATCH_FOUND,     /* Brief "Game Found!" (2-3s) */
    ONLINE_SUB_CONNECTING,      /* Connecting to game server */
    ONLINE_SUB_CONNECTED_WAITING, /* Connected, waiting for first state update */
    ONLINE_SUB_ERROR,           /* Error with retry */
} OnlineSubphase;

typedef struct OnlineUIState {
    OnlineSubphase subphase;

    /* Room code text input (for join) */
    char  room_code_buf[NET_ROOM_CODE_LEN];
    int   room_code_len;
    float cursor_blink;

    /* Created room info */
    char  created_room_code[NET_ROOM_CODE_LEN];

    /* Status / error display */
    char  status_text[128];
    char  error_text[128];

    /* Match found timer (seconds remaining) */
    float match_found_timer;
#define MATCH_FOUND_DURATION 2.0f

    /* Player slots in waiting room (populated from NET_MSG_ROOM_STATUS) */
    int  player_count;
    char player_names[NET_MAX_PLAYERS][NET_MAX_NAME_LEN];
    bool slot_is_ai[NET_MAX_PLAYERS];

    /* AI difficulty: 0 = Casual, 1 = Competitive */
    int ai_difficulty;

    /* Server connection info (from ROOM_ASSIGNED) */
    char     server_addr[NET_ADDR_LEN];
    uint16_t server_port;
    char     assigned_room_code[NET_ROOM_CODE_LEN];
    uint8_t  assigned_auth_token[NET_AUTH_TOKEN_LEN];
    bool     room_assigned;

    /* Set by main.c from settings; renderer reads to show Reconnect button */
    bool     has_reconnect;
} OnlineUIState;

/* Initialize online UI state to defaults. */
void online_ui_init(OnlineUIState *oui);

/* Poll Raylib text input for room code entry.
 * Call once per frame when subphase is ONLINE_SUB_JOIN_INPUT. */
void online_ui_update_text_input(OnlineUIState *oui, float dt);

#endif /* ONLINE_UI_H */
