/* ============================================================
 * @deps-implements: server/server_net.h
 * @deps-requires: server/server_net.h, server/room.h (Room, ConnSlotInfo,
 *                 room_manager_init, room_create, room_join, room_leave,
 *                 room_find_by_code, room_find_by_conn, room_get, room_tick_all,
 *                 room_cleanup_finished, room_reconnect, room_update_timers,
 *                 MAX_ROOMS, PlayerSlot),
 *                 server/server_game.h (ServerGame, server_game_apply_cmd),
 *                 net/socket.h (NetSocket, net_socket_*),
 *                 net/protocol.h (NetMsg, NetMsgType, NetPlayerView, NetMsgRoomStatus,
 *                 NET_MAX_PLAYERS, NET_MAX_NAME_LEN, net_build_player_view,
 *                 net_input_cmd_to_local),
 *                 core/game_state.h (PassSubphase, game_state_current_player),
 *                 core/input_cmd.h (InputCmd),
 *                 stdio.h, stdlib.h, string.h, time.h
 * @deps-last-changed: 2026-03-26 — Step 22.3: Added SV_PASS_TRANSMUTE_WAIT state mapping
 * ============================================================ */

#define _POSIX_C_SOURCE 199309L /* clock_gettime */

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
static void sv_cleanup_connection(int conn_id);
static void sv_broadcast_state(void);
static void sv_broadcast_room_status(int room_index);
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

    /* Stage 4: Tick all rooms */
    room_tick_all();

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

    /* Apply to game state */
    char err_msg[NET_MAX_CHAT_LEN] = {0};
    if (!server_game_apply_cmd(&room->game, info->seat, &cmd,
                               err_msg, sizeof(err_msg))) {
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
 * Room Status Broadcast (waiting room player list)
 * ================================================================ */

static void sv_broadcast_room_status(int room_index)
{
    Room *room = room_get(room_index);
    if (!room || room->status != ROOM_WAITING) return;

    NetMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NET_MSG_ROOM_STATUS;
    msg.room_status.player_count = (uint8_t)room->connected_count;

    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (room->slots[i].status == SLOT_CONNECTED) {
            msg.room_status.slot_occupied[i] = 1;
            memcpy(msg.room_status.player_names[i], room->slots[i].name,
                   NET_MAX_NAME_LEN);
        }
    }

    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (room->slots[i].status != SLOT_CONNECTED) continue;
        int cid = room->slots[i].conn_id;
        if (cid >= 0)
            net_socket_send_msg(&g_net, cid, &msg);
    }
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
            }

            net_socket_send_msg(&g_net, conn_id, &msg);
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
    case SV_PASS_TRANSMUTE_WAIT:
        return PASS_SUB_TRANSMUTE;
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
        room_leave(room_idx, info->seat);
        free(info);
        g_net.conns[conn_id].user_data = NULL;

        /* Notify remaining clients if still in waiting room */
        Room *after = room_get(room_idx);
        if (after && after->status == ROOM_WAITING)
            sv_broadcast_room_status(room_idx);
    }
    net_socket_close(&g_net, conn_id);
}
