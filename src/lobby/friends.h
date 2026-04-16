/* ============================================================
 * @deps-exports: friends_init, friends_on_player_authenticated,
 *                friends_on_player_disconnected, friends_on_room_created,
 *                friends_on_room_destroyed, friends_get_presence,
 *                friends_handle_search, friends_handle_request,
 *                friends_handle_accept, friends_handle_reject,
 *                friends_handle_remove, friends_handle_list_request,
 *                friends_handle_room_invite, friends_expire_room_invites,
 *                FRIENDS_MAX_INGAME
 * @deps-requires: lobby/db.h, net/protocol.h, net/socket.h
 * @deps-used-by: lobby/lobby_net.c
 * @deps-last-changed: 2026-04-06 — Task 4: Lobby Friends Module
 * ============================================================ */

#ifndef FRIENDS_H
#define FRIENDS_H

#include "db.h"
#include "../net/protocol.h"
#include "../net/socket.h"
#include <stdint.h>
#include <stdbool.h>

#define FRIENDS_MAX_INGAME 256

/* Initialize friends subsystem. Call once at lobby startup. */
void friends_init(NetSocket *net);

/* Presence lifecycle hooks — called from lobby_net.c */
void friends_on_player_authenticated(int conn_id, int32_t account_id, LobbyDB *db);
void friends_on_player_disconnected(int32_t account_id, LobbyDB *db);
void friends_on_room_created(const char *room_code, const int32_t *account_ids, int count, LobbyDB *db);
void friends_on_room_destroyed(const char *room_code, LobbyDB *db);

/* Presence query */
uint8_t friends_get_presence(int32_t account_id);

/* Message handlers — called from lobby_net.c dispatch */
void friends_handle_search(int conn_id, int32_t account_id, const NetMsgFriendSearch *msg, LobbyDB *db);
void friends_handle_request(int conn_id, int32_t account_id, const NetMsgFriendRequest *msg, LobbyDB *db);
void friends_handle_accept(int conn_id, int32_t account_id, const NetMsgFriendAccept *msg, LobbyDB *db);
void friends_handle_reject(int conn_id, int32_t account_id, const NetMsgFriendReject *msg, LobbyDB *db);
void friends_handle_remove(int conn_id, int32_t account_id, const NetMsgFriendRemove *msg, LobbyDB *db);
void friends_handle_list_request(int conn_id, int32_t account_id, LobbyDB *db);
void friends_handle_room_invite(int conn_id, int32_t account_id, const NetMsgRoomInvite *msg, LobbyDB *db);

/* Room invite cleanup */
void friends_expire_room_invites(const char *room_code);

#endif /* FRIENDS_H */
