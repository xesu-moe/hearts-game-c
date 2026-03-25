/* ============================================================
 * @deps-implements: server/room.h
 * @deps-requires: server/room.h (Room, PlayerSlot, SlotStatus, RoomStatus,
 *                 room_update_timers, room_reconnect),
 *                 server/server_game.h (server_game_init, server_game_start,
 *                 server_game_tick, server_game_is_over),
 *                 server/lobby_link.h (lobby_link_send_result),
 *                 net/protocol.h (NET_AUTH_TOKEN_LEN, NET_MAX_PLAYERS),
 *                 core/game_state.h (game_state_get_winners),
 *                 core/clock.h (FIXED_DT), fcntl.h, unistd.h
 * @deps-last-changed: 2026-03-25 — Step 18: Report results to lobby on game finish
 * ============================================================ */

#include "room.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/clock.h" /* FIXED_DT */
#include "core/game_state.h"
#include "lobby_link.h"

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

/* Generate a random session token for reconnect identification. */
static void room_generate_session_token(uint8_t token[NET_AUTH_TOKEN_LEN])
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, token, NET_AUTH_TOKEN_LEN);
        close(fd);
        if (n == NET_AUTH_TOKEN_LEN) return;
    }
    /* Fallback: rand() — less random but functional */
    for (int i = 0; i < NET_AUTH_TOKEN_LEN; i++)
        token[i] = (uint8_t)(rand() % 256);
}

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

int room_create_with_code(const char *code)
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
        printf("room_create_with_code: no free room slots (max %d)\n", MAX_ROOMS);
        return -1;
    }

    Room *room = &g_rooms[idx];
    memset(room, 0, sizeof(Room));

    /* Use the provided code instead of generating one */
    strncpy(room->code, code, ROOM_CODE_LEN - 1);
    room->code[ROOM_CODE_LEN - 1] = '\0';

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

    printf("Room %s created with lobby code (index %d, active: %d)\n",
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
    slot->disconnect_timer = -1.0f;
    memcpy(slot->lobby_token, auth_token, NET_AUTH_TOKEN_LEN);
    room_generate_session_token(slot->auth_token);

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
    } else if (room->status == ROOM_PLAYING) {
        slot->status  = SLOT_DISCONNECTED;
        slot->conn_id = -1;
        slot->disconnect_timer = 0.0f; /* start counting up */
        room->connected_count--;
        printf("Room %s: seat %d disconnected, %.0fs grace period\n",
               room->code, seat, DISCONNECT_GRACE_SEC);
        /* auth_token preserved for reconnect matching */
    } else if (room->status == ROOM_FINISHED) {
        slot->status  = SLOT_DISCONNECTED;
        slot->conn_id = -1;
        slot->disconnect_timer = -1.0f; /* no timer — room is ending */
        room->connected_count--;
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
        /* Report results to lobby before marking finished */
        int16_t scores[NET_MAX_PLAYERS];
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            scores[i] = (int16_t)room->game.gs.players[i].total_score;

        int winners[NUM_PLAYERS];
        int winner_count = game_state_get_winners(&room->game.gs, winners);
        uint8_t winner_seats[NET_MAX_PLAYERS] = {0};
        for (int i = 0; i < winner_count && i < NET_MAX_PLAYERS; i++)
            winner_seats[i] = (uint8_t)winners[i];

        uint8_t tokens[NET_MAX_PLAYERS][NET_AUTH_TOKEN_LEN];
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            memcpy(tokens[i], room->slots[i].lobby_token, NET_AUTH_TOKEN_LEN);

        lobby_link_send_result(room->code, scores, winner_seats,
                               winner_count, room->game.gs.round_number,
                               tokens);

        room->status = ROOM_FINISHED;
        printf("Room %s: game finished\n", room->code);
    }
}

void room_tick_all(void)
{
    /* Update disconnect timers and destroy abandoned rooms */
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (g_rooms[i].status == ROOM_PLAYING) {
            if (room_update_timers(i, FIXED_DT)) {
                printf("Room %s: abandoned, destroying\n", g_rooms[i].code);
                room_destroy(i);
                continue;
            }
        }
    }

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

/* ================================================================
 * Disconnect & Reconnect (Step 11)
 * ================================================================ */

bool room_update_timers(int room_index, float dt)
{
    if (room_index < 0 || room_index >= MAX_ROOMS) return false;

    Room *room = &g_rooms[room_index];
    if (room->status != ROOM_PLAYING) return false;

    /* Tick disconnect timers for each disconnected slot */
    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        PlayerSlot *slot = &room->slots[s];
        if (slot->status != SLOT_DISCONNECTED) continue;

        slot->disconnect_timer += dt;
        if (slot->disconnect_timer >= DISCONNECT_GRACE_SEC) {
            slot->status = SLOT_AI;
            slot->disconnect_timer = -1.0f;
            room->game.gs.players[s].is_human = false;
            printf("Room %s: seat %d switched to AI after %.0fs\n",
                   room->code, s, DISCONNECT_GRACE_SEC);
        }
    }

    /* Track room abandonment (0 humans connected) */
    if (room->connected_count == 0) {
        room->abandon_timer += dt;
        if (room->abandon_timer >= ROOM_ABANDON_SEC)
            return true; /* caller should destroy */
    } else {
        room->abandon_timer = 0.0f;
    }

    return false;
}

int room_reconnect(int room_index, int conn_id,
                   const uint8_t token[NET_AUTH_TOKEN_LEN])
{
    if (room_index < 0 || room_index >= MAX_ROOMS) return -1;

    Room *room = &g_rooms[room_index];
    if (room->status != ROOM_PLAYING) return -1;

    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        PlayerSlot *slot = &room->slots[s];
        if (slot->status != SLOT_DISCONNECTED && slot->status != SLOT_AI)
            continue;

        if (memcmp(slot->auth_token, token, NET_AUTH_TOKEN_LEN) != 0)
            continue;

        /* Match found — restore the slot */
        slot->status  = SLOT_CONNECTED;
        slot->conn_id = conn_id;
        slot->disconnect_timer = -1.0f;
        room->game.gs.players[s].is_human = true;
        room->connected_count++;
        room->abandon_timer = 0.0f;

        printf("Room %s: seat %d reconnected (conn %d)\n",
               room->code, s, conn_id);
        return s;
    }

    return -1;
}
