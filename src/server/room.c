/* ============================================================
 * @deps-implements: server/room.h
 * @deps-requires: server/room.h (Room, PlayerSlot, SlotStatus, RoomStatus,
 *                 ServerGame via server_game.h),
 *                 server/server_game.h (server_game_init, server_game_start,
 *                 server_game_tick, server_game_is_over),
 *                 net/protocol.h (NET_AUTH_TOKEN_LEN, NET_MAX_PLAYERS)
 * @deps-last-changed: 2026-03-23 — Initial creation (Step 5: Server Room Management)
 * ============================================================ */

#include "room.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Room Code Alphabet
 * ================================================================ */

/* Excludes ambiguous chars: O, 0, I, l, 1 */
static const char ROOM_CODE_CHARS[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
#define ROOM_CODE_ALPHABET_SIZE (int)(sizeof(ROOM_CODE_CHARS) - 1)

/* ================================================================
 * Room Manager State (file-scope)
 * ================================================================ */

static Room g_rooms[MAX_ROOMS];
static int  g_active_room_count;

/* ================================================================
 * Internal Helpers
 * ================================================================ */

/* Generate a unique 4-char room code. Returns true on success. */
static bool room_generate_code(char code[ROOM_CODE_LEN])
{
    for (int attempt = 0; attempt < 10; attempt++) {
        for (int i = 0; i < ROOM_CODE_LEN - 1; i++) {
            code[i] = ROOM_CODE_CHARS[rand() % ROOM_CODE_ALPHABET_SIZE];
        }
        code[ROOM_CODE_LEN - 1] = '\0';

        if (room_find_by_code(code) < 0) {
            return true;
        }
    }
    return false;
}

/* ================================================================
 * Room Manager API
 * ================================================================ */

void room_manager_init(void)
{
    memset(g_rooms, 0, sizeof(g_rooms));
    g_active_room_count = 0;
}

int room_create(void)
{
    /* Find first inactive slot */
    int idx = -1;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (g_rooms[i].status == ROOM_INACTIVE) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        printf("room_create: no free room slots (max %d)\n", MAX_ROOMS);
        return -1;
    }

    Room *room = &g_rooms[idx];
    memset(room, 0, sizeof(Room));

    /* Generate unique code */
    if (!room_generate_code(room->code)) {
        printf("room_create: failed to generate unique code\n");
        return -1;
    }

    /* Initialize player slots */
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        room->slots[i].status    = SLOT_EMPTY;
        room->slots[i].conn_id   = -1;
        room->slots[i].player_id = i;
    }

    /* Initialize game state */
    server_game_init(&room->game);

    room->status          = ROOM_WAITING;
    room->connected_count = 0;
    g_active_room_count++;

    printf("Room %s created (index %d, active: %d)\n",
           room->code, idx, g_active_room_count);
    return idx;
}

void room_destroy(int room_index)
{
    if (room_index < 0 || room_index >= MAX_ROOMS) return;

    Room *room = &g_rooms[room_index];
    if (room->status == ROOM_INACTIVE) return;

    printf("Room %s destroyed (index %d)\n", room->code, room_index);

    memset(room, 0, sizeof(Room));
    /* status is now ROOM_INACTIVE (0) after memset */
    g_active_room_count--;
}

/* ================================================================
 * Player Slot Operations
 * ================================================================ */

int room_join(int room_index, int conn_id,
              const uint8_t auth_token[NET_AUTH_TOKEN_LEN])
{
    if (room_index < 0 || room_index >= MAX_ROOMS) return -1;

    Room *room = &g_rooms[room_index];
    if (room->status != ROOM_WAITING) return -1;

    /* Find first empty slot */
    int seat = -1;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (room->slots[i].status == SLOT_EMPTY) {
            seat = i;
            break;
        }
    }
    if (seat < 0) return -1;

    /* Fill slot */
    PlayerSlot *slot = &room->slots[seat];
    slot->status  = SLOT_CONNECTED;
    slot->conn_id = conn_id;
    if (auth_token) {
        memcpy(slot->auth_token, auth_token, NET_AUTH_TOKEN_LEN);
    }

    room->connected_count++;
    printf("Room %s: player joined seat %d (conn %d, connected: %d/%d)\n",
           room->code, seat, conn_id, room->connected_count, NET_MAX_PLAYERS);

    /* Auto-start when all 4 slots are filled */
    if (room->connected_count == NET_MAX_PLAYERS) {
        server_game_start(&room->game);

        /* Override is_human for all connected players */
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            room->game.gs.players[i].is_human =
                (room->slots[i].status == SLOT_CONNECTED);
        }

        room->status = ROOM_PLAYING;
        printf("Room %s: all players joined, game starting\n", room->code);
    }

    return seat;
}

void room_leave(int room_index, int seat)
{
    if (room_index < 0 || room_index >= MAX_ROOMS) return;
    if (seat < 0 || seat >= NET_MAX_PLAYERS) return;

    Room *room = &g_rooms[room_index];
    if (room->status == ROOM_INACTIVE) return;

    PlayerSlot *slot = &room->slots[seat];
    if (slot->status != SLOT_CONNECTED) return;

    printf("Room %s: player left seat %d (conn %d)\n",
           room->code, seat, slot->conn_id);

    if (room->status == ROOM_WAITING) {
        slot->status  = SLOT_EMPTY;
        slot->conn_id = -1;
        memset(slot->auth_token, 0, NET_AUTH_TOKEN_LEN);
        room->connected_count--;

        if (room->connected_count == 0) {
            printf("Room %s: all players left, destroying\n", room->code);
            room_destroy(room_index);
        }
    } else if (room->status == ROOM_PLAYING || room->status == ROOM_FINISHED) {
        slot->status  = SLOT_DISCONNECTED;
        slot->conn_id = -1;
        room->connected_count--;
        /* AI takeover and disconnect timers added in Step 11 */
    }
}

/* ================================================================
 * Queries
 * ================================================================ */

int room_find_by_code(const char *code)
{
    if (!code) return -1;

    for (int i = 0; i < MAX_ROOMS; i++) {
        if (g_rooms[i].status != ROOM_INACTIVE &&
            strcmp(g_rooms[i].code, code) == 0) {
            return i;
        }
    }
    return -1;
}

int room_find_by_conn(int conn_id, int *out_seat)
{
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (g_rooms[i].status == ROOM_INACTIVE) continue;

        for (int s = 0; s < NET_MAX_PLAYERS; s++) {
            if (g_rooms[i].slots[s].conn_id == conn_id &&
                g_rooms[i].slots[s].status == SLOT_CONNECTED) {
                if (out_seat) *out_seat = s;
                return i;
            }
        }
    }
    return -1;
}

Room *room_get(int room_index)
{
    if (room_index < 0 || room_index >= MAX_ROOMS) return NULL;
    if (g_rooms[room_index].status == ROOM_INACTIVE) return NULL;
    return &g_rooms[room_index];
}

int room_active_count(void)
{
    return g_active_room_count;
}

/* ================================================================
 * Game Tick
 * ================================================================ */

void room_tick(int room_index)
{
    if (room_index < 0 || room_index >= MAX_ROOMS) return;

    Room *room = &g_rooms[room_index];
    if (room->status != ROOM_PLAYING) return;

    server_game_tick(&room->game);

    if (server_game_is_over(&room->game)) {
        room->status = ROOM_FINISHED;
        printf("Room %s: game finished\n", room->code);
    }
}

void room_tick_all(void)
{
    /* Tick all playing rooms */
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (g_rooms[i].status == ROOM_PLAYING) {
            room_tick(i);
        }
    }

    /* Destroy all finished rooms (immediate cleanup) */
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (g_rooms[i].status == ROOM_FINISHED) {
            room_destroy(i);
        }
    }
}
