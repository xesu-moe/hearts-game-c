/* ============================================================
 * @deps-implements: server/server_net.h
 * @deps-requires: server/server_net.h, server/room.h (Room, room_manager_init, room_create, room_tick_all),
 *                 server/server_game.h (ServerGame, server_game_apply_cmd),
 *                 net/socket.h (NetSocket, net_socket_*),
 *                 net/protocol.h (NetMsg, NetMsgType, NetPlayerView (with rogue/duel_revealed_card),
 *                 NET_MAX_PLAYERS, net_build_player_view, net_input_cmd_to_local),
 *                 core/game_state.h (PassSubphase, game_state_current_player),
 *                 core/input_cmd.h (InputCmd), stdio.h, stdlib.h, string.h, time.h
 * @deps-last-changed: 2026-04-04 — Updated net_build_player_view for NetPlayerView rogue/duel card fields
 * ============================================================ */

#include "server_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "room.h"
#include "server_game.h"
#include "net/socket.h"
#include "net/protocol.h"
#include "core/game_state.h"
#include "core/input_cmd.h"

/* ================================================================
 * File-scope state
 * ================================================================ */

static NetSocket g_net;
static bool      g_listening;

/* ================================================================
 * Internal helpers — forward declarations
 * ================================================================ */

static void sv_handle_message(int conn_id, const NetMsg *msg);
static void sv_handle_handshake(int conn_id, const NetMsgHandshake *hs);
static void sv_handle_input_cmd(int conn_id, const NetInputCmd *net_cmd);
static void sv_handle_ping(int conn_id, const NetMsgPing *ping);
static void sv_handle_request_add_ai(int conn_id);
static void sv_handle_request_remove_ai(int conn_id);
static void sv_handle_request_start_game(int conn_id, const NetMsgStartGame *msg);
static void sv_cleanup_connection(int conn_id);
static void sv_broadcast_state(void);
static void sv_broadcast_room_status(int room_index);
static void sv_broadcast_chat_rich(Room *room, const char *text,
                                   uint8_t r, uint8_t g, uint8_t b,
                                   int16_t transmute_id,
                                   const char *highlight);
static void sv_broadcast_chat(Room *room, const char *text);
static void sv_cleanup_finished_rooms(void);
static uint8_t sv_pass_substate_to_client(ServerPassSubstate ss);

static uint32_t get_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ================================================================
 * Public API
 * ================================================================ */

void server_net_init(int max_connections)
{
    net_socket_init(&g_net, max_connections);
    room_manager_init();
    g_listening = false;
    printf("[net] Initialized (max %d connections)\n", max_connections);
}

void server_net_shutdown(void)
{
    /* Free ConnSlotInfo for all active connections */
    for (int i = 0; i < g_net.max_conns; i++) {
        if (net_socket_state(&g_net, i) != NET_CONN_DISCONNECTED &&
            g_net.conns[i].user_data != NULL) {
            free(g_net.conns[i].user_data);
            g_net.conns[i].user_data = NULL;
        }
    }
    net_socket_shutdown(&g_net);
    g_listening = false;
    printf("[net] Shutdown\n");
}

bool server_net_listen(uint16_t port)
{
    if (net_socket_listen(&g_net, port) < 0) {
        fprintf(stderr, "[net] Failed to listen on port %d\n", port);
        return false;
    }
    g_listening = true;
    printf("[net] Listening on port %d\n", port);
    return true;
}

void server_net_update(void)
{
    if (!g_listening) return;

    /* Stage 1: Poll all sockets */
    net_socket_update(&g_net);

    /* Stage 2: Accept new connections */
    int new_conn;
    while ((new_conn = net_socket_accept(&g_net)) >= 0) {
        printf("[net] New connection: conn %d (%s)\n",
               new_conn, g_net.conns[new_conn].remote_addr);
    }

    /* Stage 3: Process incoming messages from all connections */
    for (int i = 0; i < g_net.max_conns; i++) {
        if (net_socket_state(&g_net, i) != NET_CONN_CONNECTED) continue;

        NetMsg msg;
        while (net_socket_recv_msg(&g_net, i, &msg)) {
            sv_handle_message(i, &msg);
        }
    }

    /* Stage 3.5: Snapshot pass_ready[] before tick for AI/timeout detection */
    for (int r = 0; r < MAX_ROOMS; r++) {
        Room *room = room_get(r);
        if (!room || room->status != ROOM_PLAYING) continue;
        ServerGame *sg = &room->game;
        if (sg->pass_substate == SV_PASS_CARD_SELECT) {
            for (int p = 0; p < NUM_PLAYERS; p++)
                sg->prev_pass_ready[p] = sg->gs.pass_ready[p];
        }
    }

    /* Stage 4: Tick all rooms */
    room_tick_all();

    /* Stage 4.5a: Broadcast pass confirmations for AI/timeout confirms */
    for (int r = 0; r < MAX_ROOMS; r++) {
        Room *room = room_get(r);
        if (!room || room->status != ROOM_PLAYING) continue;
        ServerGame *sg = &room->game;
        /* Check if any pass_ready changed during the tick (AI auto-select
         * or timeout auto-select). Human confirms are handled in
         * sv_handle_input_cmd before the tick. */
        if (sg->pass_substate == SV_PASS_CARD_SELECT ||
            sg->pass_substate == SV_PASS_EXECUTE) {
            for (int p = 0; p < NUM_PLAYERS; p++) {
                if (sg->gs.pass_ready[p] && !sg->prev_pass_ready[p]) {
                    NetMsg pc_msg;
                    memset(&pc_msg, 0, sizeof(pc_msg));
                    pc_msg.type = NET_MSG_PASS_CONFIRMED;
                    pc_msg.pass_confirmed.seat = (uint8_t)p;
                    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
                        if (room->slots[s].status != SLOT_CONNECTED) continue;
                        int cid = room->slots[s].conn_id;
                        if (cid >= 0)
                            net_socket_send_msg(&g_net, cid, &pc_msg);
                    }
                }
            }
        }
    }

    /* Stage 4.5b: Drain game event chat queues */
    for (int r = 0; r < MAX_ROOMS; r++) {
        Room *room = room_get(r);
        if (!room || (room->status != ROOM_PLAYING &&
                      room->status != ROOM_FINISHED)) continue;
        ServerGame *sg = &room->game;
        for (int ci = 0; ci < sg->chat_count; ci++) {
            sv_broadcast_chat_rich(room, sg->chat_queue[ci],
                                   sg->chat_colors[ci][0],
                                   sg->chat_colors[ci][1],
                                   sg->chat_colors[ci][2],
                                   sg->chat_transmute_ids[ci],
                                   sg->chat_highlights[ci]);
        }
        sg->chat_count = 0;
    }

    /* Stage 5: Broadcast state updates (includes FINISHED rooms for game-over) */
    sv_broadcast_state();

    /* Stage 6: Clean up finished rooms and their stale ConnSlotInfo */
    sv_cleanup_finished_rooms();

    /* Stage 7: Detect and handle disconnected connections */
    for (int i = 0; i < g_net.max_conns; i++) {
        if (net_socket_state(&g_net, i) == NET_CONN_DISCONNECTED &&
            g_net.conns[i].user_data != NULL) {
            printf("[net] Connection %d dropped\n", i);
            sv_cleanup_connection(i);
        }
    }
}

/* ================================================================
 * Message Routing
 * ================================================================ */

static void sv_handle_message(int conn_id, const NetMsg *msg)
{
    switch (msg->type) {
    case NET_MSG_HANDSHAKE:
        sv_handle_handshake(conn_id, &msg->handshake);
        break;
    case NET_MSG_INPUT_CMD:
        sv_handle_input_cmd(conn_id, &msg->input_cmd);
        break;
    case NET_MSG_PING:
        sv_handle_ping(conn_id, &msg->ping);
        break;
    case NET_MSG_DISCONNECT:
        printf("[net] Client %d sent disconnect\n", conn_id);
        sv_cleanup_connection(conn_id);
        break;
    case NET_MSG_REQUEST_ADD_AI:
        sv_handle_request_add_ai(conn_id);
        break;
    case NET_MSG_REQUEST_REMOVE_AI:
        sv_handle_request_remove_ai(conn_id);
        break;
    case NET_MSG_REQUEST_START_GAME:
        sv_handle_request_start_game(conn_id, &msg->start_game);
        break;
    default:
        printf("[net] Unexpected message type %d from conn %d\n",
               msg->type, conn_id);
        break;
    }
}

/* ================================================================
 * Handshake
 * ================================================================ */

static void sv_handle_handshake(int conn_id, const NetMsgHandshake *hs)
{
    /* Check protocol version */
    if (hs->protocol_version != PROTOCOL_VERSION) {
        printf("[net] Version mismatch from conn %d: client=%d, server=%d\n",
               conn_id, hs->protocol_version, PROTOCOL_VERSION);
        NetMsg reply;
        memset(&reply, 0, sizeof(reply));
        reply.type = NET_MSG_HANDSHAKE_REJECT;
        reply.handshake_reject.server_version = PROTOCOL_VERSION;
        reply.handshake_reject.reason = NET_REJECT_VERSION_MISMATCH;
        net_socket_send_msg(&g_net, conn_id, &reply);
        net_socket_close(&g_net, conn_id);
        return;
    }

    /* Check if already in a room */
    if (g_net.conns[conn_id].user_data != NULL) {
        printf("[net] Conn %d already has a room assignment\n", conn_id);
        return;
    }

    /* Determine room: empty code = create new, otherwise join existing */
    int room_idx = -1;
    bool is_empty_code = true;
    for (int i = 0; i < NET_ROOM_CODE_LEN; i++) {
        if (hs->room_code[i] != '\0' && hs->room_code[i] != ' ') {
            is_empty_code = false;
            break;
        }
    }

    if (is_empty_code) {
        room_idx = room_create();
        if (room_idx < 0) {
            printf("[net] Server full, rejecting conn %d\n", conn_id);
            NetMsg reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = NET_MSG_HANDSHAKE_REJECT;
            reply.handshake_reject.server_version = PROTOCOL_VERSION;
            reply.handshake_reject.reason = NET_REJECT_ROOM_FULL;
            net_socket_send_msg(&g_net, conn_id, &reply);
            net_socket_close(&g_net, conn_id);
            return;
        }
    } else {
        room_idx = room_find_by_code(hs->room_code);
        if (room_idx < 0) {
            printf("[net] Invalid room code '%.4s' from conn %d\n",
                   hs->room_code, conn_id);
            NetMsg reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = NET_MSG_HANDSHAKE_REJECT;
            reply.handshake_reject.server_version = PROTOCOL_VERSION;
            reply.handshake_reject.reason = NET_REJECT_INVALID_ROOM;
            net_socket_send_msg(&g_net, conn_id, &reply);
            net_socket_close(&g_net, conn_id);
            return;
        }

        /* If room is PLAYING, try reconnect by session token */
        Room *playing_room = room_get(room_idx);
        if (playing_room && playing_room->status == ROOM_PLAYING) {
            int seat = room_reconnect(room_idx, conn_id, hs->auth_token);
            if (seat >= 0) {
                ConnSlotInfo *info = malloc(sizeof(ConnSlotInfo));
                if (!info) {
                    fprintf(stderr, "[net] malloc failed for ConnSlotInfo\n");
                    room_leave(room_idx, seat);
                    net_socket_close(&g_net, conn_id);
                    return;
                }
                info->room_index = room_idx;
                info->seat = seat;
                g_net.conns[conn_id].user_data = info;

                NetMsg reply;
                memset(&reply, 0, sizeof(reply));
                reply.type = NET_MSG_HANDSHAKE_ACK;
                reply.handshake_ack.protocol_version = PROTOCOL_VERSION;
                reply.handshake_ack.assigned_seat = (uint8_t)seat;
                memcpy(reply.handshake_ack.session_token,
                       playing_room->slots[seat].auth_token,
                       NET_AUTH_TOKEN_LEN);
                net_socket_send_msg(&g_net, conn_id, &reply);

                printf("[net] Conn %d reconnected to room %s seat %d\n",
                       conn_id, playing_room->code, seat);

                /* Broadcast reconnect to other players */
                {
                    char rmsg[NET_MAX_CHAT_LEN];
                    snprintf(rmsg, sizeof(rmsg), "%s reconnected",
                             playing_room->slots[seat].name);
                    sv_broadcast_chat(playing_room, rmsg);
                }
                return;
            }
            /* No matching token — can't join mid-game */
            NetMsg reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = NET_MSG_HANDSHAKE_REJECT;
            reply.handshake_reject.server_version = PROTOCOL_VERSION;
            reply.handshake_reject.reason = NET_REJECT_ROOM_FULL;
            net_socket_send_msg(&g_net, conn_id, &reply);
            net_socket_close(&g_net, conn_id);
            return;
        }
    }

    /* Join the room */
    int seat = room_join(room_idx, conn_id, hs->auth_token, hs->username);
    if (seat < 0) {
        printf("[net] Room full, rejecting conn %d\n", conn_id);
        NetMsg reply;
        memset(&reply, 0, sizeof(reply));
        reply.type = NET_MSG_HANDSHAKE_REJECT;
        reply.handshake_reject.server_version = PROTOCOL_VERSION;
        reply.handshake_reject.reason = NET_REJECT_ROOM_FULL;
        net_socket_send_msg(&g_net, conn_id, &reply);
        net_socket_close(&g_net, conn_id);
        return;
    }

    /* Set up conn→room mapping */
    ConnSlotInfo *info = malloc(sizeof(ConnSlotInfo));
    if (!info) {
        fprintf(stderr, "[net] malloc failed for ConnSlotInfo\n");
        room_leave(room_idx, seat);
        NetMsg reply;
        memset(&reply, 0, sizeof(reply));
        reply.type = NET_MSG_HANDSHAKE_REJECT;
        reply.handshake_reject.server_version = PROTOCOL_VERSION;
        reply.handshake_reject.reason = NET_REJECT_ROOM_FULL;
        net_socket_send_msg(&g_net, conn_id, &reply);
        net_socket_close(&g_net, conn_id);
        return;
    }
    info->room_index = room_idx;
    info->seat = seat;
    g_net.conns[conn_id].user_data = info;

    /* Send handshake ACK with session token */
    Room *room = room_get(room_idx);
    NetMsg reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = NET_MSG_HANDSHAKE_ACK;
    reply.handshake_ack.protocol_version = PROTOCOL_VERSION;
    reply.handshake_ack.assigned_seat = (uint8_t)seat;
    if (room) {
        memcpy(reply.handshake_ack.session_token,
               room->slots[seat].auth_token, NET_AUTH_TOKEN_LEN);
    }
    net_socket_send_msg(&g_net, conn_id, &reply);

    printf("[net] Conn %d joined room %s seat %d\n",
           conn_id, room ? room->code : "???", seat);

    /* Notify all clients in WAITING room about the updated player list.
     * Note: when the 4th player joins, room_join() auto-starts the game
     * (WAITING→PLAYING), so sv_broadcast_room_status() is a no-op.
     * This is fine — clients exit the waiting room on the first
     * NET_MSG_STATE_UPDATE which arrives in the same server tick. */
    sv_broadcast_room_status(room_idx);
}

/* ================================================================
 * Input Command
 * ================================================================ */

static void sv_handle_input_cmd(int conn_id, const NetInputCmd *net_cmd)
{
    ConnSlotInfo *info = (ConnSlotInfo *)g_net.conns[conn_id].user_data;
    if (!info) {
        printf("[net] Input from conn %d with no room assignment\n", conn_id);
        return;
    }

    Room *room = room_get(info->room_index);
    if (!room || room->status != ROOM_PLAYING) {
        return;
    }

    /* Convert wire format to local InputCmd */
    InputCmd cmd;
    net_input_cmd_to_local(net_cmd, &cmd);
    cmd.source_player = info->seat; /* Enforce seat — never trust client */

    /* Snapshot pass substate before applying (CONFIRM may be the trigger) */
    ServerPassSubstate pre_pass_sub = room->game.pass_substate;

    /* Apply to game state */
    char err_msg[NET_MAX_CHAT_LEN] = {0};
    bool accepted = server_game_apply_cmd(&room->game, info->seat, &cmd,
                                          err_msg, sizeof(err_msg));
    if (accepted) {
        /* Broadcast per-player pass confirmation for async toss animation */
        if (cmd.type == INPUT_CMD_CONFIRM &&
            pre_pass_sub == SV_PASS_CARD_SELECT) {
            NetMsg pc_msg;
            memset(&pc_msg, 0, sizeof(pc_msg));
            pc_msg.type = NET_MSG_PASS_CONFIRMED;
            pc_msg.pass_confirmed.seat = (uint8_t)info->seat;
            for (int s = 0; s < NET_MAX_PLAYERS; s++) {
                if (room->slots[s].status != SLOT_CONNECTED) continue;
                int cid = room->slots[s].conn_id;
                if (cid >= 0)
                    net_socket_send_msg(&g_net, cid, &pc_msg);
            }
        }
    } else {
        /* Send error to client (skip silent rejections like CONFIRM) */
        if (err_msg[0] != '\0') {
            NetMsg err;
            memset(&err, 0, sizeof(err));
            err.type = NET_MSG_ERROR;
            err.error.code = 1;
            strncpy(err.error.message, err_msg, NET_MAX_CHAT_LEN - 1);
            net_socket_send_msg(&g_net, conn_id, &err);
        }
    }
}

/* ================================================================
 * Ping/Pong
 * ================================================================ */

static void sv_handle_ping(int conn_id, const NetMsgPing *ping)
{
    NetMsg reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = NET_MSG_PONG;
    reply.pong.sequence = ping->sequence;
    reply.pong.echo_timestamp_ms = ping->timestamp_ms;
    reply.pong.server_timestamp_ms = get_monotonic_ms();
    net_socket_send_msg(&g_net, conn_id, &reply);
}

/* ================================================================
 * Add AI to Waiting Room
 * ================================================================ */

static void sv_handle_request_add_ai(int conn_id)
{
    ConnSlotInfo *info = (ConnSlotInfo *)g_net.conns[conn_id].user_data;
    if (!info) {
        printf("[net] REQUEST_ADD_AI from unregistered conn %d\n", conn_id);
        return;
    }

    /* Only seat 0 (room creator) can add AI */
    if (info->seat != 0) {
        printf("[net] REQUEST_ADD_AI from non-creator seat %d, ignoring\n",
               info->seat);
        return;
    }

    int room_idx = info->room_index;
    Room *room = room_get(room_idx);
    if (!room || room->status != ROOM_WAITING) {
        printf("[net] REQUEST_ADD_AI: room not in WAITING state\n");
        return;
    }

    int ai_seat = room_add_ai(room_idx);
    if (ai_seat < 0) {
        printf("[net] REQUEST_ADD_AI: no empty slots\n");
        return;
    }

    /* Broadcast updated room status to all connected clients */
    sv_broadcast_room_status(room_idx);
}

/* ================================================================
 * Remove AI (creator request)
 * ================================================================ */

static void sv_handle_request_remove_ai(int conn_id)
{
    ConnSlotInfo *info = (ConnSlotInfo *)g_net.conns[conn_id].user_data;
    if (!info) {
        printf("[net] REQUEST_REMOVE_AI from unregistered conn %d\n", conn_id);
        return;
    }

    /* Only seat 0 (room creator) can remove AI */
    if (info->seat != 0) {
        printf("[net] REQUEST_REMOVE_AI from non-creator seat %d, ignoring\n",
               info->seat);
        return;
    }

    int room_idx = info->room_index;
    Room *room = room_get(room_idx);
    if (!room || room->status != ROOM_WAITING) {
        printf("[net] REQUEST_REMOVE_AI: room not in WAITING state\n");
        return;
    }

    int removed_seat = room_remove_ai(room_idx);
    if (removed_seat < 0) {
        printf("[net] REQUEST_REMOVE_AI: no AI slots to remove\n");
        return;
    }

    /* Broadcast updated room status to all connected clients */
    sv_broadcast_room_status(room_idx);
}

/* ================================================================
 * Start Game (creator request)
 * ================================================================ */

static void sv_handle_request_start_game(int conn_id, const NetMsgStartGame *start_msg)
{
    ConnSlotInfo *info = (ConnSlotInfo *)g_net.conns[conn_id].user_data;
    if (!info) {
        printf("[net] REQUEST_START_GAME from unregistered conn %d\n", conn_id);
        return;
    }

    /* Only seat 0 (room creator) can start the game */
    if (info->seat != 0) {
        printf("[net] REQUEST_START_GAME from non-creator seat %d, ignoring\n",
               info->seat);
        return;
    }

    int room_idx = info->room_index;
    Room *room = room_get(room_idx);
    if (!room || room->status != ROOM_WAITING) {
        printf("[net] REQUEST_START_GAME: room not in WAITING state\n");
        return;
    }

    /* Store game options before starting (clamp to valid ranges) */
    room->ai_difficulty  = start_msg->ai_difficulty > 1 ? 0 : start_msg->ai_difficulty;
    room->timer_option   = start_msg->timer_option > GAME_OPT_TIMER_MAX ? 0 : start_msg->timer_option;
    room->point_goal_idx = start_msg->point_goal_idx > GAME_OPT_POINT_MAX ? GAME_OPT_POINT_MAX : start_msg->point_goal_idx;
    room->gamemode       = start_msg->gamemode > GAME_OPT_MODE_MAX ? 0 : start_msg->gamemode;

    if (room_start(room_idx) < 0) {
        printf("[net] REQUEST_START_GAME: room_start failed (need 4 players)\n");
        return;
    }

    printf("[net] Room %s: game started by creator (conn %d, ai_diff=%d, timer=%d, points=%d, mode=%d)\n",
           room->code, conn_id, start_msg->ai_difficulty,
           start_msg->timer_option, start_msg->point_goal_idx, start_msg->gamemode);
    /* State update will be broadcast in the next sv_broadcast_state() cycle */
}

/* ================================================================
 * Room Status Broadcast (waiting room player list)
 * ================================================================ */

static void sv_broadcast_room_status(int room_index)
{
    Room *room = room_get(room_index);
    if (!room || room->status != ROOM_WAITING) return;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_ROOM_STATUS;

    /* Count total occupied slots (humans + AI) */
    int occupied = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (room->slots[i].status != SLOT_EMPTY) {
            msg.room_status.slot_occupied[i] = 1;
            msg.room_status.slot_is_ai[i] =
                (room->slots[i].status == SLOT_AI) ? 1 : 0;
            memcpy(msg.room_status.player_names[i], room->slots[i].name,
                   NET_MAX_NAME_LEN);
            occupied++;
        }
    }
    msg.room_status.player_count = (uint8_t)occupied;

    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (room->slots[i].status != SLOT_CONNECTED) continue;
        int cid = room->slots[i].conn_id;
        if (cid >= 0)
            net_socket_send_msg(&g_net, cid, &msg);
    }
}

/* Send a system chat message to all connected players in a room. */
static void sv_broadcast_chat_rich(Room *room, const char *text,
                                   uint8_t r, uint8_t g, uint8_t b,
                                   int16_t transmute_id,
                                   const char *highlight)
{
    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_CHAT;
    msg.chat.seat = 0xFF; /* system message */
    snprintf(msg.chat.text, NET_MAX_CHAT_LEN, "%s", text);
    msg.chat.color_r = r;
    msg.chat.color_g = g;
    msg.chat.color_b = b;
    msg.chat.transmute_id = transmute_id;
    if (highlight && highlight[0]) {
        strncpy(msg.chat.highlight, highlight, sizeof(msg.chat.highlight) - 1);
        msg.chat.highlight[sizeof(msg.chat.highlight) - 1] = '\0';
    }

    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        if (room->slots[s].status != SLOT_CONNECTED) continue;
        int cid = room->slots[s].conn_id;
        if (cid >= 0)
            net_socket_send_msg(&g_net, cid, &msg);
    }
}

static void sv_broadcast_chat(Room *room, const char *text)
{
    sv_broadcast_chat_rich(room, text, 255, 161, 0, -1, NULL); /* ORANGE */
}

/* ================================================================
 * State Broadcast
 * ================================================================ */

static void sv_broadcast_state(void)
{
    for (int r = 0; r < MAX_ROOMS; r++) {
        Room *room = room_get(r);
        if (!room || (room->status != ROOM_PLAYING &&
                      room->status != ROOM_FINISHED)) continue;
        if (room->connected_count == 0) continue;

        /* Skip broadcast if game state hasn't changed since last send */
        {
            int cur_phase = (int)room->game.gs.phase;
            int cur_np    = room->game.gs.current_trick.num_played;
            int cur_tp    = room->game.gs.tricks_played;
            int cur_rnd   = room->game.gs.round_number;
            bool changed  = room->force_broadcast ||
                            room->game.state_dirty ||
                            cur_phase != room->last_broadcast.phase ||
                            cur_np    != room->last_broadcast.num_played ||
                            cur_tp    != room->last_broadcast.tricks_played ||
                            cur_rnd   != room->last_broadcast.round_number;
            if (!changed) {
                for (int i = 0; i < NET_MAX_PLAYERS && !changed; i++)
                    changed = room->game.gs.players[i].hand.count
                              != room->last_broadcast.hand_counts[i];
            }
            if (!changed) continue;
            room->last_broadcast.phase         = cur_phase;
            room->last_broadcast.num_played    = cur_np;
            room->last_broadcast.tricks_played = cur_tp;
            room->last_broadcast.round_number  = cur_rnd;
            for (int i = 0; i < NET_MAX_PLAYERS; i++)
                room->last_broadcast.hand_counts[i] =
                    room->game.gs.players[i].hand.count;
            room->force_broadcast = false;
            room->game.state_dirty = false;
        }

        for (int s = 0; s < NET_MAX_PLAYERS; s++) {
            if (room->slots[s].status != SLOT_CONNECTED) continue;

            int conn_id = room->slots[s].conn_id;
            if (conn_id < 0) continue;

            NetMsg msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = NET_MSG_STATE_UPDATE;

            net_build_player_view(
                &msg.state_update,
                &room->game.gs,
                &room->game.p2,
                0, /* flow_step: not used on server */
                0.0f, /* turn_timer: added in Step 11 */
                sv_pass_substate_to_client(room->game.pass_substate),
                room->game.dealer_player,
                game_state_current_player(&room->game.gs),
                s /* seat */
            );

            /* Set configured turn time limit from room options */
            {
                static const int timer_bonus[] = { 0, 5, 10, 15, 20 };
                int ti = room->game.timer_option;
                if (ti > GAME_OPT_TIMER_MAX) ti = 0;
                msg.state_update.turn_time_limit = 30.0f + (float)timer_bonus[ti];
            }

            /* Fill trick transmute info from server game state */
            const TrickTransmuteInfo *tti = &room->game.current_tti;
            for (int i = 0; i < CARDS_PER_TRICK; i++) {
                msg.state_update.trick_transmutes.transmutation_ids[i] =
                    (int16_t)tti->transmutation_ids[i];
                msg.state_update.trick_transmutes.transmuter_player[i] =
                    (int8_t)tti->transmuter_player[i];
                msg.state_update.trick_transmutes.resolved_effects[i] =
                    (uint8_t)tti->resolved_effects[i];
                msg.state_update.trick_transmutes.fogged[i] =
                    tti->fogged[i];
                msg.state_update.trick_transmutes.fog_transmuter[i] =
                    (int8_t)tti->fog_transmuter[i];
            }

            /* Server-authoritative trick winner */
            msg.state_update.trick_winner =
                (int8_t)room->game.last_trick_winner;

            if (net_socket_send_msg(&g_net, conn_id, &msg) < 0) {
                /* Send buffer full — state update dropped. Protocol
                 * self-heals on next successful send (full snapshot). */
                static uint32_t last_warn_ms = 0;
                uint32_t now_ms = get_monotonic_ms();
                if (now_ms - last_warn_ms > 1000) {
                    printf("[net] WARN: state update dropped for conn %d "
                           "(send buffer full)\n", conn_id);
                    last_warn_ms = now_ms;
                }
            }
        }

        /* Send game-over message with ELO data when room is finished */
        if (room->status == ROOM_FINISHED) {
            NetMsg go_msg;
            memset(&go_msg, 0, sizeof(go_msg));
            go_msg.type = NET_MSG_GAME_OVER;
            for (int i = 0; i < NET_MAX_PLAYERS; i++)
                go_msg.game_over.final_scores[i] =
                    (int16_t)room->game.gs.players[i].total_score;
            int winners[NUM_PLAYERS];
            int wc = game_state_get_winners(&room->game.gs, winners);
            go_msg.game_over.winner_count = (uint8_t)wc;
            for (int w = 0; w < wc && w < NET_MAX_PLAYERS; w++)
                go_msg.game_over.winners[w] = (uint8_t)winners[w];
            go_msg.game_over.has_elo = room->elo_received;
            if (room->elo_received) {
                for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                    go_msg.game_over.prev_elo[i] = room->elo_prev[i];
                    go_msg.game_over.new_elo[i]  = room->elo_new[i];
                }
            }
            for (int s = 0; s < NET_MAX_PLAYERS; s++) {
                if (room->slots[s].status != SLOT_CONNECTED) continue;
                int cid = room->slots[s].conn_id;
                if (cid >= 0)
                    net_socket_send_msg(&g_net, cid, &go_msg);
            }
        }
    }
}

static uint8_t sv_pass_substate_to_client(ServerPassSubstate ss)
{
    switch (ss) {
    case SV_PASS_DEALER_DIR:
    case SV_PASS_DEALER_AMT:
    case SV_PASS_DEALER_CONFIRM:
        return PASS_SUB_DEALER;
    case SV_PASS_CONTRACT_DRAFT:
        return PASS_SUB_CONTRACT;
    case SV_PASS_CARD_SELECT:
        return PASS_SUB_CARD_PASS;
    default:
        return 0;
    }
}

/* ================================================================
 * Finished Room Cleanup
 * ================================================================ */

static void sv_cleanup_finished_rooms(void)
{
    for (int r = 0; r < MAX_ROOMS; r++) {
        Room *room = room_get(r);
        if (!room || room->status != ROOM_FINISHED) continue;

        /* Keep room alive until ELO response arrives (or timeout) */
        if (!room->elo_received) {
            room->elo_wait_timer += (1.0f / 60.0f);
            if (room->elo_wait_timer < 10.0f) continue; /* wait up to 10s */
        }

        /* Free stale ConnSlotInfo for all connections pointing at this room */
        for (int i = 0; i < g_net.max_conns; i++) {
            ConnSlotInfo *info = (ConnSlotInfo *)g_net.conns[i].user_data;
            if (!info || info->room_index != r) continue;

            free(info);
            g_net.conns[i].user_data = NULL;
            net_socket_close(&g_net, i);
        }

        room_destroy(r);
    }
}

/* ================================================================
 * Connection Cleanup
 * ================================================================ */

static void sv_cleanup_connection(int conn_id)
{
    ConnSlotInfo *info = (ConnSlotInfo *)g_net.conns[conn_id].user_data;
    if (info) {
        int room_idx = info->room_index;
        Room *room = room_get(room_idx);
        printf("[net] Cleaning up conn %d (room %s, seat %d)\n",
               conn_id,
               room ? room->code : "???",
               info->seat);

        /* Capture name before room_leave may clear the slot */
        char disc_name[NET_MAX_NAME_LEN];
        disc_name[0] = '\0';
        bool was_playing = false;
        if (room && room->status == ROOM_PLAYING) {
            was_playing = true;
            snprintf(disc_name, sizeof(disc_name), "%s",
                     room->slots[info->seat].name);
        }

        room_leave(room_idx, info->seat);
        free(info);
        g_net.conns[conn_id].user_data = NULL;

        /* Notify remaining clients */
        Room *after = room_get(room_idx);
        if (after && after->status == ROOM_WAITING)
            sv_broadcast_room_status(room_idx);

        /* Broadcast disconnect to remaining players in active game */
        if (was_playing && after && after->status == ROOM_PLAYING &&
            disc_name[0]) {
            char dmsg[NET_MAX_CHAT_LEN];
            snprintf(dmsg, sizeof(dmsg), "%s disconnected", disc_name);
            sv_broadcast_chat(after, dmsg);
        }
    }
    net_socket_close(&g_net, conn_id);
}
