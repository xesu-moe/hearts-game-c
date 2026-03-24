/* ============================================================
 * @deps-exports: Room, PlayerSlot, ConnSlotInfo, SlotStatus, RoomStatus,
 *                room_manager_init(), room_create(), room_destroy(),
 *                room_join(), room_leave(), room_find_by_code(),
 *                room_find_by_conn(), room_get(), room_active_count(),
 *                room_tick(), room_tick_all(),
 *                ROOM_CODE_LEN, MAX_ROOMS
 * @deps-requires: server/server_game.h (ServerGame, server_game_init,
 *                 server_game_start, server_game_tick, server_game_is_over),
 *                 net/protocol.h (NET_AUTH_TOKEN_LEN, NET_MAX_PLAYERS)
 * @deps-last-changed: 2026-03-23 — Initial creation (Step 5: Server Room Management)
 * ============================================================ */

#ifndef ROOM_H
#define ROOM_H

#include <stdbool.h>
#include <stdint.h>

#include "server_game.h"
#include "net/protocol.h"

/* ================================================================
 * Constants
 * ================================================================ */

#define ROOM_CODE_LEN 5   /* 4 chars + NUL */
#define MAX_ROOMS     100

/* ================================================================
 * Enums
 * ================================================================ */

typedef enum SlotStatus {
    SLOT_EMPTY        = 0,
    SLOT_CONNECTED    = 1,
    SLOT_DISCONNECTED = 2, /* mid-game disconnect (AI takeover in Step 11) */
    SLOT_AI           = 3
} SlotStatus;

typedef enum RoomStatus {
    ROOM_INACTIVE = 0, /* slot in g_rooms[] is unused */
    ROOM_WAITING  = 1, /* created, accepting joins */
    ROOM_PLAYING  = 2, /* game active */
    ROOM_FINISHED = 3  /* game over, awaiting cleanup */
} RoomStatus;

/* ================================================================
 * Structs
 * ================================================================ */

/* Stored in NetConn.user_data to map a connection back to its room and seat.
 * Allocated/freed by caller (server_net.c in Step 6). */
typedef struct ConnSlotInfo {
    int room_index;
    int seat;
} ConnSlotInfo;

typedef struct PlayerSlot {
    SlotStatus status;
    int        conn_id;   /* index into NetSocket.conns[], -1 if not connected */
    int        player_id; /* seat 0-3, same as array index */
    uint8_t    auth_token[NET_AUTH_TOKEN_LEN]; /* stub — no-op validation */
} PlayerSlot;

typedef struct Room {
    RoomStatus  status;
    char        code[ROOM_CODE_LEN];
    PlayerSlot  slots[NET_MAX_PLAYERS];
    ServerGame  game;
    int         connected_count; /* number of SLOT_CONNECTED players */
} Room;

/* ================================================================
 * Room Manager API
 * ================================================================ */

/* Initialize the room manager. Call once at server startup. */
void room_manager_init(void);

/* Create a new room with a unique 4-char code.
 * Returns room index (0..MAX_ROOMS-1) on success, -1 if full. */
int room_create(void);

/* Destroy a room, setting it to INACTIVE.
 * Does NOT close network connections or free ConnSlotInfo. */
void room_destroy(int room_index);

/* ================================================================
 * Player Slot Operations
 * ================================================================ */

/* Join a player to a room. Finds first EMPTY slot, sets CONNECTED.
 * If all 4 slots fill, auto-starts the game (WAITING → PLAYING).
 * Returns assigned seat (0-3) on success, -1 if full or not WAITING. */
int room_join(int room_index, int conn_id,
              const uint8_t auth_token[NET_AUTH_TOKEN_LEN]);

/* Remove a player from a room.
 * WAITING: sets slot EMPTY; destroys room if all left.
 * PLAYING: sets slot DISCONNECTED. */
void room_leave(int room_index, int seat);

/* ================================================================
 * Queries
 * ================================================================ */

/* Find a room by its 4-char code. Returns room index or -1. */
int room_find_by_code(const char *code);

/* Find a room and seat by connection id. Linear scan.
 * Sets *out_seat if found. Returns room index or -1. */
int room_find_by_conn(int conn_id, int *out_seat);

/* Get a pointer to a room by index.
 * Returns NULL if out of range or INACTIVE. */
Room *room_get(int room_index);

/* Return the number of currently active rooms. */
int room_active_count(void);

/* ================================================================
 * Game Tick
 * ================================================================ */

/* Tick the room's game if PLAYING.
 * If game ends, transitions to FINISHED. */
void room_tick(int room_index);

/* Tick all PLAYING rooms, then destroy all FINISHED rooms. */
void room_tick_all(void);

#endif /* ROOM_H */
