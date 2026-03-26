/* ============================================================
 * Client — Lobby Connection Manager
 *
 * Manages the client's TCP connection to the lobby server.
 * Handles registration, challenge-response login, and
 * username changes. Runs alongside client_net (game server).
 *
 * @deps-exports: LobbyClientState, LobbyClientInfo (elo_rating: int32_t)
 * @deps-requires: net/identity.h (Identity),
 *                 net/protocol.h (NET_AUTH_TOKEN_LEN, NET_MAX_NAME_LEN)
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-26 — Step 22.5: LobbyClientInfo.elo_rating uint16_t→int32_t
 * ============================================================ */

#ifndef LOBBY_CLIENT_H
#define LOBBY_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "net/identity.h"
#include "net/protocol.h"

/* ================================================================
 * State Machine
 * ================================================================ */

typedef enum LobbyClientState {
    LOBBY_DISCONNECTED = 0,
    LOBBY_CONNECTING,    /* TCP connect in progress */
    LOBBY_CONNECTED,     /* TCP up, idle (awaiting register/login call) */
    LOBBY_REGISTERING,   /* NET_MSG_REGISTER sent, awaiting ACK */
    LOBBY_LOGGING_IN,    /* NET_MSG_LOGIN sent, awaiting challenge */
    LOBBY_CHALLENGED,    /* Signed response sent, awaiting LOGIN_ACK */
    LOBBY_AUTHENTICATED, /* Login complete, token stored */
    LOBBY_CREATING_ROOM, /* Awaiting ROOM_ASSIGNED after create */
    LOBBY_JOINING_ROOM,  /* Awaiting ROOM_ASSIGNED after join */
    LOBBY_QUEUED,        /* In matchmaking queue */
    LOBBY_ERROR          /* Error with message */
} LobbyClientState;

/* ================================================================
 * Post-Login Info
 * ================================================================ */

typedef struct LobbyClientInfo {
    uint8_t  auth_token[NET_AUTH_TOKEN_LEN];
    int32_t  elo_rating;
    uint32_t games_played;
    uint32_t games_won;
    char     username[NET_MAX_NAME_LEN];
} LobbyClientInfo;

/* ================================================================
 * API
 * ================================================================ */

void lobby_client_init(void);
void lobby_client_shutdown(void);

/* Connect to lobby server. Non-blocking. */
void lobby_client_connect(const char *ip, uint16_t port);
void lobby_client_disconnect(void);

/* Per-frame update: poll socket, handle auth state machine.
 * identity is needed to sign login challenges. */
void lobby_client_update(float dt, const Identity *id);

/* Initiate registration (sends NET_MSG_REGISTER with username + public key). */
void lobby_client_register(const char *username, const Identity *id);

/* Initiate login (sends NET_MSG_LOGIN with username). */
void lobby_client_login(const char *username);

/* Change username on lobby server (sends NET_MSG_CHANGE_USERNAME). */
void lobby_client_change_username(const char *new_username);

/* Room and matchmaking operations (requires AUTHENTICATED state) */
void lobby_client_create_room(void);
void lobby_client_join_room(const char *code);
void lobby_client_queue_matchmake(void);
void lobby_client_queue_cancel(void);

/* Query state */
LobbyClientState       lobby_client_state(void);
const LobbyClientInfo *lobby_client_info(void);  /* valid when AUTHENTICATED */
const char            *lobby_client_error_msg(void);

/* Room assignment query (set when ROOM_ASSIGNED received).
 * Returns true if a room assignment is pending.
 * consume copies data and clears the flag. */
bool lobby_client_has_room_assignment(void);
void lobby_client_consume_room_assignment(char *addr_out, uint16_t *port_out,
                                          char *room_code_out,
                                          uint8_t *token_out);

#endif /* LOBBY_CLIENT_H */
