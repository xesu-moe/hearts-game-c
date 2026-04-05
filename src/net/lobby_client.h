/* ============================================================
 * Client — Lobby Connection Manager
 *
 * Manages the client's TCP connection to the lobby server.
 * Handles registration, challenge-response login, and
 * username changes. Runs alongside client_net (game server).
 *
 * @deps-exports: LobbyClientState, LobbyClientInfo (elo_rating: int32_t),
 *                lobby_client_init/shutdown, lobby_client_connect/disconnect,
 *                lobby_client_update, lobby_client_register, lobby_client_login,
 *                lobby_client_change_username, lobby_client_create_room,
 *                lobby_client_join_room, lobby_client_queue_matchmake,
 *                lobby_client_queue_cancel, lobby_client_cancel_create,
 *                lobby_client_cancel_join, lobby_client_state, lobby_client_info,
 *                lobby_client_error_msg, lobby_client_clear_error,
 *                lobby_client_has_room_assignment, lobby_client_consume_room_assignment
 * @deps-requires: net/identity.h (Identity),
 *                 net/protocol.h (NET_AUTH_TOKEN_LEN, NET_MAX_NAME_LEN, NET_CHALLENGE_LEN, NET_ROOM_CODE_LEN, NET_ADDR_LEN)
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-30 — Added lobby_client_clear_error() function
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
void lobby_client_cancel_create(void);
void lobby_client_cancel_join(void);

/* Query state */
LobbyClientState       lobby_client_state(void);
const LobbyClientInfo *lobby_client_info(void);  /* valid when AUTHENTICATED */
const char            *lobby_client_error_msg(void);
void                   lobby_client_clear_error(void);

/* Room assignment query (set when ROOM_ASSIGNED received).
 * Returns true if a room assignment is pending.
 * consume copies data and clears the flag. */
bool lobby_client_has_room_assignment(void);
void lobby_client_consume_room_assignment(char *addr_out, uint16_t *port_out,
                                          char *room_code_out,
                                          uint8_t *token_out);

/* ================================================================
 * Stats & Leaderboard
 * ================================================================ */

typedef struct PlayerFullStats {
    int32_t  elo_rating;
    uint32_t games_played, games_won;
    int32_t  total_score;
    uint32_t moon_shots, qos_caught, contracts_fulfilled, perfect_rounds;
    uint32_t hearts_collected, tricks_won;
    int32_t  best_score, worst_score;
    float    avg_placement;
} PlayerFullStats;

typedef struct LeaderboardEntry {
    char     username[NET_MAX_NAME_LEN];
    int32_t  elo_rating;
    uint32_t games_played, games_won;
} LeaderboardEntry;

typedef struct LeaderboardData {
    LeaderboardEntry entries[LEADERBOARD_MAX_ENTRIES];
    int      count;
    uint16_t player_rank;
    int32_t  player_elo;
} LeaderboardData;

/* Request stats/leaderboard from lobby (requires AUTHENTICATED). */
void lobby_client_request_stats(void);
void lobby_client_request_leaderboard(void);

/* Poll for responses. Returns true and fills *out if data arrived.
 * Consumes the ready flag (one-shot). */
bool lobby_client_has_stats(PlayerFullStats *out);
bool lobby_client_has_leaderboard(LeaderboardData *out);

#endif /* LOBBY_CLIENT_H */
