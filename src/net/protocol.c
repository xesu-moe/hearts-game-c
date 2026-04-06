/* ============================================================
 * @deps-implements: protocol.h
 * @deps-requires: protocol.h (NetPlayerView.rogue_revealed_card, duel_revealed_card),
 *                 core/card.h, core/input_cmd.h (INPUT_CMD_COUNT),
 *                 core/game_state.h, phase2/phase2_state.h, string.h, stdio.h
 * @deps-last-changed: 2026-04-04 — Updated NetPlayerView serialization for rogue/duel revealed cards
 * ============================================================ */

#include "net/protocol.h"

#include "core/game_state.h"
#include "core/input_cmd.h"
#include "phase2/phase2_state.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
 * Byte-Packing Helpers (little-endian, memcpy-based)
 * ================================================================ */

static inline void write_u8(uint8_t *buf, size_t *off, uint8_t v)
{
    buf[*off] = v;
    *off += 1;
}

static inline void write_i8(uint8_t *buf, size_t *off, int8_t v)
{
    buf[*off] = (uint8_t)v;
    *off += 1;
}

static inline void write_u16(uint8_t *buf, size_t *off, uint16_t v)
{
    memcpy(buf + *off, &v, 2);
    *off += 2;
}

static inline void write_i16(uint8_t *buf, size_t *off, int16_t v)
{
    memcpy(buf + *off, &v, 2);
    *off += 2;
}

static inline void write_u32(uint8_t *buf, size_t *off, uint32_t v)
{
    memcpy(buf + *off, &v, 4);
    *off += 4;
}

static inline void write_f32(uint8_t *buf, size_t *off, float v)
{
    memcpy(buf + *off, &v, 4);
    *off += 4;
}

static inline void write_bool(uint8_t *buf, size_t *off, bool v)
{
    buf[*off] = v ? 1 : 0;
    *off += 1;
}

static inline void write_bytes(uint8_t *buf, size_t *off, const void *src,
                               size_t n)
{
    memcpy(buf + *off, src, n);
    *off += n;
}

static inline uint8_t read_u8(const uint8_t *buf, size_t *off)
{
    uint8_t v = buf[*off];
    *off += 1;
    return v;
}

static inline int8_t read_i8(const uint8_t *buf, size_t *off)
{
    int8_t v = (int8_t)buf[*off];
    *off += 1;
    return v;
}

static inline uint16_t read_u16(const uint8_t *buf, size_t *off)
{
    uint16_t v;
    memcpy(&v, buf + *off, 2);
    *off += 2;
    return v;
}

static inline int16_t read_i16(const uint8_t *buf, size_t *off)
{
    int16_t v;
    memcpy(&v, buf + *off, 2);
    *off += 2;
    return v;
}

static inline uint32_t read_u32(const uint8_t *buf, size_t *off)
{
    uint32_t v;
    memcpy(&v, buf + *off, 4);
    *off += 4;
    return v;
}

static inline float read_f32(const uint8_t *buf, size_t *off)
{
    float v;
    memcpy(&v, buf + *off, 4);
    *off += 4;
    return v;
}

static inline bool read_bool(const uint8_t *buf, size_t *off)
{
    bool v = buf[*off] != 0;
    *off += 1;
    return v;
}

static inline void read_bytes(const uint8_t *buf, size_t *off, void *dst,
                              size_t n)
{
    memcpy(dst, buf + *off, n);
    *off += n;
}

/* ================================================================
 * NetCard Serialization
 * ================================================================ */

static void write_card(uint8_t *buf, size_t *off, NetCard c)
{
    write_i8(buf, off, c.suit);
    write_i8(buf, off, c.rank);
}

static NetCard read_card(const uint8_t *buf, size_t *off)
{
    NetCard c;
    c.suit = read_i8(buf, off);
    c.rank = read_i8(buf, off);
    return c;
}

/* ================================================================
 * Framing
 * ================================================================ */

int net_frame_write(uint8_t *buf, size_t buf_size, const uint8_t *payload,
                    size_t payload_len)
{
    size_t total = NET_FRAME_HEADER_SIZE + payload_len;
    if (total > buf_size || payload_len > NET_MAX_MSG_SIZE)
        return -1;
    uint32_t len = (uint32_t)payload_len;
    memcpy(buf, &len, 4);
    if (payload_len > 0)
        memcpy(buf + NET_FRAME_HEADER_SIZE, payload, payload_len);
    return (int)total;
}

int net_frame_read(const uint8_t *buf, size_t buf_len,
                   const uint8_t **out_payload, size_t *out_payload_len)
{
    if (buf_len < NET_FRAME_HEADER_SIZE)
        return 0;
    uint32_t payload_len;
    memcpy(&payload_len, buf, 4);
    if (payload_len > NET_MAX_MSG_SIZE)
        return -1;
    size_t total = NET_FRAME_HEADER_SIZE + payload_len;
    if (buf_len < total)
        return 0;
    *out_payload = buf + NET_FRAME_HEADER_SIZE;
    *out_payload_len = payload_len;
    return (int)total;
}

/* ================================================================
 * Per-Message Serialization (static helpers)
 * ================================================================ */

/* --- Connection messages --- */

static int ser_handshake(const NetMsgHandshake *m, uint8_t *buf, size_t len)
{
    size_t need = 2 + NET_AUTH_TOKEN_LEN + NET_ROOM_CODE_LEN + NET_MAX_NAME_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    write_u16(buf, &off, m->protocol_version);
    write_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    write_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    write_bytes(buf, &off, m->username, NET_MAX_NAME_LEN);
    return (int)off;
}

static int deser_handshake(NetMsgHandshake *m, const uint8_t *buf, size_t len)
{
    size_t need = 2 + NET_AUTH_TOKEN_LEN + NET_ROOM_CODE_LEN + NET_MAX_NAME_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    m->protocol_version = read_u16(buf, &off);
    read_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    read_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    m->room_code[NET_ROOM_CODE_LEN - 1] = '\0';
    read_bytes(buf, &off, m->username, NET_MAX_NAME_LEN);
    m->username[NET_MAX_NAME_LEN - 1] = '\0';
    return (int)off;
}

static int ser_handshake_ack(const NetMsgHandshakeAck *m, uint8_t *buf,
                             size_t len)
{
    size_t need = 3 + NET_AUTH_TOKEN_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    write_u16(buf, &off, m->protocol_version);
    write_u8(buf, &off, m->assigned_seat);
    write_bytes(buf, &off, m->session_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int deser_handshake_ack(NetMsgHandshakeAck *m, const uint8_t *buf,
                               size_t len)
{
    size_t need = 3 + NET_AUTH_TOKEN_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    m->protocol_version = read_u16(buf, &off);
    m->assigned_seat = read_u8(buf, &off);
    read_bytes(buf, &off, m->session_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int ser_handshake_reject(const NetMsgHandshakeReject *m, uint8_t *buf,
                                size_t len)
{
    if (len < 3)
        return -1;
    size_t off = 0;
    write_u16(buf, &off, m->server_version);
    write_u8(buf, &off, m->reason);
    return (int)off;
}

static int deser_handshake_reject(NetMsgHandshakeReject *m, const uint8_t *buf,
                                  size_t len)
{
    if (len < 3)
        return -1;
    size_t off = 0;
    m->server_version = read_u16(buf, &off);
    m->reason = read_u8(buf, &off);
    return (int)off;
}

static int ser_ping(const NetMsgPing *m, uint8_t *buf, size_t len)
{
    if (len < 8)
        return -1;
    size_t off = 0;
    write_u32(buf, &off, m->sequence);
    write_u32(buf, &off, m->timestamp_ms);
    return (int)off;
}

static int deser_ping(NetMsgPing *m, const uint8_t *buf, size_t len)
{
    if (len < 8)
        return -1;
    size_t off = 0;
    m->sequence = read_u32(buf, &off);
    m->timestamp_ms = read_u32(buf, &off);
    return (int)off;
}

static int ser_pong(const NetMsgPong *m, uint8_t *buf, size_t len)
{
    if (len < 12)
        return -1;
    size_t off = 0;
    write_u32(buf, &off, m->sequence);
    write_u32(buf, &off, m->echo_timestamp_ms);
    write_u32(buf, &off, m->server_timestamp_ms);
    return (int)off;
}

static int deser_pong(NetMsgPong *m, const uint8_t *buf, size_t len)
{
    if (len < 12)
        return -1;
    size_t off = 0;
    m->sequence = read_u32(buf, &off);
    m->echo_timestamp_ms = read_u32(buf, &off);
    m->server_timestamp_ms = read_u32(buf, &off);
    return (int)off;
}

static int ser_disconnect(const NetMsgDisconnect *m, uint8_t *buf, size_t len)
{
    if (len < 1)
        return -1;
    size_t off = 0;
    write_u8(buf, &off, m->reason);
    return (int)off;
}

static int deser_disconnect(NetMsgDisconnect *m, const uint8_t *buf,
                            size_t len)
{
    if (len < 1)
        return -1;
    size_t off = 0;
    m->reason = read_u8(buf, &off);
    return (int)off;
}

static int ser_error(const NetMsgError *m, uint8_t *buf, size_t len)
{
    if (len < 1 + NET_MAX_CHAT_LEN)
        return -1;
    size_t off = 0;
    write_u8(buf, &off, m->code);
    write_bytes(buf, &off, m->message, NET_MAX_CHAT_LEN);
    return (int)off;
}

static int deser_error(NetMsgError *m, const uint8_t *buf, size_t len)
{
    if (len < 1 + NET_MAX_CHAT_LEN)
        return -1;
    size_t off = 0;
    m->code = read_u8(buf, &off);
    read_bytes(buf, &off, m->message, NET_MAX_CHAT_LEN);
    m->message[NET_MAX_CHAT_LEN - 1] = '\0';
    return (int)off;
}

static int ser_chat(const NetMsgChat *m, uint8_t *buf, size_t len)
{
    size_t need = 1 + NET_MAX_CHAT_LEN + 3 + 2 + 32;
    if (len < need)
        return -1;
    size_t off = 0;
    write_u8(buf, &off, m->seat);
    write_bytes(buf, &off, m->text, NET_MAX_CHAT_LEN);
    write_u8(buf, &off, m->color_r);
    write_u8(buf, &off, m->color_g);
    write_u8(buf, &off, m->color_b);
    write_i16(buf, &off, m->transmute_id);
    write_bytes(buf, &off, m->highlight, 32);
    return (int)off;
}

static int deser_chat(NetMsgChat *m, const uint8_t *buf, size_t len)
{
    size_t need = 1 + NET_MAX_CHAT_LEN + 3 + 2 + 32;
    if (len < need)
        return -1;
    size_t off = 0;
    m->seat = read_u8(buf, &off);
    read_bytes(buf, &off, m->text, NET_MAX_CHAT_LEN);
    m->text[NET_MAX_CHAT_LEN - 1] = '\0';
    m->color_r = read_u8(buf, &off);
    m->color_g = read_u8(buf, &off);
    m->color_b = read_u8(buf, &off);
    m->transmute_id = read_i16(buf, &off);
    read_bytes(buf, &off, m->highlight, 32);
    m->highlight[31] = '\0';
    return (int)off;
}

static int ser_room_status(const NetMsgRoomStatus *m, uint8_t *buf, size_t len)
{
    size_t need = 1 + NET_MAX_PLAYERS * NET_MAX_NAME_LEN + NET_MAX_PLAYERS * 2;
    if (len < need)
        return -1;
    size_t off = 0;
    write_u8(buf, &off, m->player_count);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_bytes(buf, &off, m->player_names[i], NET_MAX_NAME_LEN);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u8(buf, &off, m->slot_occupied[i]);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u8(buf, &off, m->slot_is_ai[i]);
    return (int)off;
}

static int deser_room_status(NetMsgRoomStatus *m, const uint8_t *buf, size_t len)
{
    size_t need = 1 + NET_MAX_PLAYERS * NET_MAX_NAME_LEN + NET_MAX_PLAYERS * 2;
    if (len < need)
        return -1;
    size_t off = 0;
    m->player_count = read_u8(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        read_bytes(buf, &off, m->player_names[i], NET_MAX_NAME_LEN);
        m->player_names[i][NET_MAX_NAME_LEN - 1] = '\0';
    }
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->slot_occupied[i] = read_u8(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->slot_is_ai[i] = read_u8(buf, &off);
    return (int)off;
}

/* --- Game messages --- */

static int ser_input_cmd(const NetInputCmd *m, uint8_t *buf, size_t len)
{
    if (len < 2)
        return -1;
    size_t off = 0;
    write_u8(buf, &off, m->type);
    write_u8(buf, &off, m->source_player);

    /* Write union payload based on type */
    switch (m->type) {
    case INPUT_CMD_SELECT_CARD:
    case INPUT_CMD_PLAY_CARD:
        if (len < off + 3)
            return -1;
        write_i8(buf, &off, m->card.card_index);
        write_card(buf, &off, m->card.card);
        break;
    case INPUT_CMD_SELECT_CONTRACT:
        if (len < off + 2)
            return -1;
        write_i16(buf, &off, m->contract.pair_index);
        break;
    case INPUT_CMD_SELECT_TRANSMUTATION:
        if (len < off + 1)
            return -1;
        write_i8(buf, &off, m->transmute_select.inv_slot);
        break;
    case INPUT_CMD_APPLY_TRANSMUTATION:
        if (len < off + 3)
            return -1;
        write_i8(buf, &off, m->transmute_apply.hand_index);
        write_i8(buf, &off, m->transmute_apply.card_suit);
        write_i8(buf, &off, m->transmute_apply.card_rank);
        break;
    case INPUT_CMD_ROGUE_REVEAL:
        if (len < off + 2)
            return -1;
        write_i8(buf, &off, m->rogue_reveal.target_player);
        write_i8(buf, &off, m->rogue_reveal.suit);
        break;
    case INPUT_CMD_DUEL_PICK:
        if (len < off + 1)
            return -1;
        write_i8(buf, &off, m->duel_pick.target_player);
        break;
    case INPUT_CMD_DUEL_GIVE:
        if (len < off + 1)
            return -1;
        write_i8(buf, &off, m->duel_give.hand_index);
        break;
    case INPUT_CMD_DEALER_DIR:
        if (len < off + 1)
            return -1;
        write_i8(buf, &off, m->dealer_dir.direction);
        break;
    case INPUT_CMD_DEALER_AMT:
        if (len < off + 1)
            return -1;
        write_i8(buf, &off, m->dealer_amt.amount);
        break;
    default:
        /* Commands with no payload (CONFIRM, CANCEL, START_GAME, QUIT,
         * DUEL_RETURN, DEALER_CONFIRM) — just type + source_player */
        break;
    }
    return (int)off;
}

static int deser_input_cmd(NetInputCmd *m, const uint8_t *buf, size_t len)
{
    if (len < 2)
        return -1;
    size_t off = 0;
    memset(m, 0, sizeof(*m));
    m->type = read_u8(buf, &off);
    m->source_player = read_u8(buf, &off);

    switch (m->type) {
    case INPUT_CMD_SELECT_CARD:
    case INPUT_CMD_PLAY_CARD:
        if (len < off + 3)
            return -1;
        m->card.card_index = read_i8(buf, &off);
        m->card.card = read_card(buf, &off);
        break;
    case INPUT_CMD_SELECT_CONTRACT:
        if (len < off + 2)
            return -1;
        m->contract.pair_index = read_i16(buf, &off);
        break;
    case INPUT_CMD_SELECT_TRANSMUTATION:
        if (len < off + 1)
            return -1;
        m->transmute_select.inv_slot = read_i8(buf, &off);
        break;
    case INPUT_CMD_APPLY_TRANSMUTATION:
        if (len < off + 3)
            return -1;
        m->transmute_apply.hand_index = read_i8(buf, &off);
        m->transmute_apply.card_suit = read_i8(buf, &off);
        m->transmute_apply.card_rank = read_i8(buf, &off);
        break;
    case INPUT_CMD_ROGUE_REVEAL:
        if (len < off + 2)
            return -1;
        m->rogue_reveal.target_player = read_i8(buf, &off);
        m->rogue_reveal.suit = read_i8(buf, &off);
        break;
    case INPUT_CMD_DUEL_PICK:
        if (len < off + 1)
            return -1;
        m->duel_pick.target_player = read_i8(buf, &off);
        break;
    case INPUT_CMD_DUEL_GIVE:
        if (len < off + 1)
            return -1;
        m->duel_give.hand_index = read_i8(buf, &off);
        break;
    case INPUT_CMD_DEALER_DIR:
        if (len < off + 1)
            return -1;
        m->dealer_dir.direction = read_i8(buf, &off);
        break;
    case INPUT_CMD_DEALER_AMT:
        if (len < off + 1)
            return -1;
        m->dealer_amt.amount = read_i8(buf, &off);
        break;
    default:
        break;
    }
    return (int)off;
}

static int ser_round_start(const NetMsgRoundStart *m, uint8_t *buf, size_t len)
{
    if (len < 5)
        return -1;
    size_t off = 0;
    write_u16(buf, &off, m->round_number);
    write_u8(buf, &off, m->pass_direction);
    write_u8(buf, &off, m->pass_count);
    write_i8(buf, &off, m->dealer_seat);
    return (int)off;
}

static int deser_round_start(NetMsgRoundStart *m, const uint8_t *buf,
                             size_t len)
{
    if (len < 5)
        return -1;
    size_t off = 0;
    m->round_number = read_u16(buf, &off);
    m->pass_direction = read_u8(buf, &off);
    m->pass_count = read_u8(buf, &off);
    m->dealer_seat = read_i8(buf, &off);
    return (int)off;
}

static int ser_trick_result(const NetMsgTrickResult *m, uint8_t *buf,
                            size_t len)
{
    if (len < 3)
        return -1;
    size_t off = 0;
    write_u8(buf, &off, m->winner_seat);
    write_u8(buf, &off, m->points);
    write_u8(buf, &off, m->trick_num);
    return (int)off;
}

static int deser_trick_result(NetMsgTrickResult *m, const uint8_t *buf,
                              size_t len)
{
    if (len < 3)
        return -1;
    size_t off = 0;
    m->winner_seat = read_u8(buf, &off);
    m->points = read_u8(buf, &off);
    m->trick_num = read_u8(buf, &off);
    return (int)off;
}

static int ser_game_over(const NetMsgGameOver *m, uint8_t *buf, size_t len)
{
    /* base: scores(8) + winners(4) + winner_count(1) + has_elo(1) = 14 */
    size_t need = NET_MAX_PLAYERS * 2 + NET_MAX_PLAYERS + 1 + 1;
    if (m->has_elo) need += NET_MAX_PLAYERS * 4 * 2;
    if (len < need) return -1;
    size_t off = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_i16(buf, &off, m->final_scores[i]);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u8(buf, &off, m->winners[i]);
    write_u8(buf, &off, m->winner_count);
    write_u8(buf, &off, m->has_elo ? 1 : 0);
    if (m->has_elo) {
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            write_u32(buf, &off, (uint32_t)m->prev_elo[i]);
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            write_u32(buf, &off, (uint32_t)m->new_elo[i]);
    }
    return (int)off;
}

static int deser_game_over(NetMsgGameOver *m, const uint8_t *buf, size_t len)
{
    size_t base = NET_MAX_PLAYERS * 2 + NET_MAX_PLAYERS + 1 + 1;
    if (len < base) return -1;
    size_t off = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->final_scores[i] = read_i16(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->winners[i] = read_u8(buf, &off);
    m->winner_count = read_u8(buf, &off);
    m->has_elo = read_u8(buf, &off) != 0;
    if (m->has_elo) {
        if (len < base + NET_MAX_PLAYERS * 4 * 2) return -1;
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            m->prev_elo[i] = (int32_t)read_u32(buf, &off);
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            m->new_elo[i] = (int32_t)read_u32(buf, &off);
    } else {
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            m->prev_elo[i] = -1;
            m->new_elo[i] = -1;
        }
    }
    return (int)off;
}

static int ser_phase_change(const NetMsgPhaseChange *m, uint8_t *buf,
                            size_t len)
{
    if (len < 1)
        return -1;
    size_t off = 0;
    write_u8(buf, &off, m->new_phase);
    return (int)off;
}

static int deser_phase_change(NetMsgPhaseChange *m, const uint8_t *buf,
                              size_t len)
{
    if (len < 1)
        return -1;
    size_t off = 0;
    m->new_phase = read_u8(buf, &off);
    return (int)off;
}

/* --- Lobby messages --- */

static int ser_register(const NetMsgRegister *m, uint8_t *buf, size_t len)
{
    size_t need = NET_MAX_NAME_LEN + NET_ED25519_PK_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->username, NET_MAX_NAME_LEN);
    write_bytes(buf, &off, m->public_key, NET_ED25519_PK_LEN);
    return (int)off;
}

static int deser_register(NetMsgRegister *m, const uint8_t *buf, size_t len)
{
    size_t need = NET_MAX_NAME_LEN + NET_ED25519_PK_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->username, NET_MAX_NAME_LEN);
    m->username[NET_MAX_NAME_LEN - 1] = '\0';
    read_bytes(buf, &off, m->public_key, NET_ED25519_PK_LEN);
    return (int)off;
}

static int ser_login(const NetMsgLogin *m, uint8_t *buf, size_t len)
{
    if (len < NET_MAX_NAME_LEN)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->username, NET_MAX_NAME_LEN);
    return (int)off;
}

static int deser_login(NetMsgLogin *m, const uint8_t *buf, size_t len)
{
    if (len < NET_MAX_NAME_LEN)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->username, NET_MAX_NAME_LEN);
    m->username[NET_MAX_NAME_LEN - 1] = '\0';
    return (int)off;
}

static int ser_login_challenge(const NetMsgLoginChallenge *m, uint8_t *buf, size_t len)
{
    if (len < NET_CHALLENGE_LEN)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->nonce, NET_CHALLENGE_LEN);
    return (int)off;
}

static int deser_login_challenge(NetMsgLoginChallenge *m, const uint8_t *buf, size_t len)
{
    if (len < NET_CHALLENGE_LEN)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->nonce, NET_CHALLENGE_LEN);
    return (int)off;
}

static int ser_login_response(const NetMsgLoginResponse *m, uint8_t *buf, size_t len)
{
    if (len < NET_ED25519_SIG_LEN)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->signature, NET_ED25519_SIG_LEN);
    return (int)off;
}

static int deser_login_response(NetMsgLoginResponse *m, const uint8_t *buf, size_t len)
{
    if (len < NET_ED25519_SIG_LEN)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->signature, NET_ED25519_SIG_LEN);
    return (int)off;
}

static int ser_login_ack(const NetMsgLoginAck *m, uint8_t *buf, size_t len)
{
    size_t need = NET_AUTH_TOKEN_LEN + 4 + 4 + 4;
    if (len < need)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    write_u32(buf, &off, (uint32_t)m->elo_rating);
    write_u32(buf, &off, m->games_played);
    write_u32(buf, &off, m->games_won);
    return (int)off;
}

static int deser_login_ack(NetMsgLoginAck *m, const uint8_t *buf, size_t len)
{
    size_t need = NET_AUTH_TOKEN_LEN + 4 + 4 + 4;
    if (len < need)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    m->elo_rating = (int32_t)read_u32(buf, &off);
    m->games_played = read_u32(buf, &off);
    m->games_won = read_u32(buf, &off);
    return (int)off;
}

static int ser_create_room(const NetMsgCreateRoom *m, uint8_t *buf, size_t len)
{
    if (len < NET_AUTH_TOKEN_LEN)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int deser_create_room(NetMsgCreateRoom *m, const uint8_t *buf,
                             size_t len)
{
    if (len < NET_AUTH_TOKEN_LEN)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int ser_join_room(const NetMsgJoinRoom *m, uint8_t *buf, size_t len)
{
    size_t need = NET_AUTH_TOKEN_LEN + NET_ROOM_CODE_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    write_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    return (int)off;
}

static int deser_join_room(NetMsgJoinRoom *m, const uint8_t *buf, size_t len)
{
    size_t need = NET_AUTH_TOKEN_LEN + NET_ROOM_CODE_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    read_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    m->room_code[NET_ROOM_CODE_LEN - 1] = '\0';
    return (int)off;
}

static int ser_room_assigned(const NetMsgRoomAssigned *m, uint8_t *buf,
                             size_t len)
{
    size_t need = NET_ADDR_LEN + 2 + NET_ROOM_CODE_LEN + NET_AUTH_TOKEN_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->server_addr, NET_ADDR_LEN);
    write_u16(buf, &off, m->server_port);
    write_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    write_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int deser_room_assigned(NetMsgRoomAssigned *m, const uint8_t *buf,
                               size_t len)
{
    size_t need = NET_ADDR_LEN + 2 + NET_ROOM_CODE_LEN + NET_AUTH_TOKEN_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->server_addr, NET_ADDR_LEN);
    m->server_addr[NET_ADDR_LEN - 1] = '\0';
    m->server_port = read_u16(buf, &off);
    read_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    m->room_code[NET_ROOM_CODE_LEN - 1] = '\0';
    read_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int ser_queue_matchmake(const NetMsgQueueMatchmake *m, uint8_t *buf,
                               size_t len)
{
    if (len < NET_AUTH_TOKEN_LEN)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int deser_queue_matchmake(NetMsgQueueMatchmake *m, const uint8_t *buf,
                                 size_t len)
{
    if (len < NET_AUTH_TOKEN_LEN)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int ser_queue_status(const NetMsgQueueStatus *m, uint8_t *buf,
                            size_t len)
{
    if (len < 4)
        return -1;
    size_t off = 0;
    write_u16(buf, &off, m->position);
    write_u16(buf, &off, m->estimated_wait_secs);
    return (int)off;
}

static int deser_queue_status(NetMsgQueueStatus *m, const uint8_t *buf,
                              size_t len)
{
    if (len < 4)
        return -1;
    size_t off = 0;
    m->position = read_u16(buf, &off);
    m->estimated_wait_secs = read_u16(buf, &off);
    return (int)off;
}

static int ser_change_username(const NetMsgChangeUsername *m, uint8_t *buf,
                               size_t len)
{
    size_t need = NET_AUTH_TOKEN_LEN + NET_MAX_NAME_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    write_bytes(buf, &off, (const uint8_t *)m->new_username, NET_MAX_NAME_LEN);
    return (int)off;
}

static int deser_change_username(NetMsgChangeUsername *m, const uint8_t *buf,
                                  size_t len)
{
    size_t need = NET_AUTH_TOKEN_LEN + NET_MAX_NAME_LEN;
    if (len < need)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    read_bytes(buf, &off, (uint8_t *)m->new_username, NET_MAX_NAME_LEN);
    m->new_username[NET_MAX_NAME_LEN - 1] = '\0';
    return (int)off;
}

/* --- Stats & Leaderboard messages --- */

static int ser_stats_request(const NetMsgStatsRequest *m, uint8_t *buf,
                             size_t len)
{
    if (len < NET_AUTH_TOKEN_LEN)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int deser_stats_request(NetMsgStatsRequest *m, const uint8_t *buf,
                               size_t len)
{
    if (len < NET_AUTH_TOKEN_LEN)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int ser_stats_response(const NetMsgStatsResponse *m, uint8_t *buf,
                              size_t len)
{
    /* 4+4+4+4 + 4*6 + 4+4+2 = 50 bytes */
    size_t need = 50;
    if (len < need)
        return -1;
    size_t off = 0;
    write_u32(buf, &off, (uint32_t)m->elo_rating);
    write_u32(buf, &off, m->games_played);
    write_u32(buf, &off, m->games_won);
    write_u32(buf, &off, (uint32_t)m->total_score);
    write_u32(buf, &off, m->moon_shots);
    write_u32(buf, &off, m->qos_caught);
    write_u32(buf, &off, m->contracts_fulfilled);
    write_u32(buf, &off, m->perfect_rounds);
    write_u32(buf, &off, m->hearts_collected);
    write_u32(buf, &off, m->tricks_won);
    write_u32(buf, &off, (uint32_t)m->best_score);
    write_u32(buf, &off, (uint32_t)m->worst_score);
    write_u16(buf, &off, m->avg_placement_x100);
    return (int)off;
}

static int deser_stats_response(NetMsgStatsResponse *m, const uint8_t *buf,
                                size_t len)
{
    size_t need = 50;
    if (len < need)
        return -1;
    size_t off = 0;
    m->elo_rating          = (int32_t)read_u32(buf, &off);
    m->games_played        = read_u32(buf, &off);
    m->games_won           = read_u32(buf, &off);
    m->total_score         = (int32_t)read_u32(buf, &off);
    m->moon_shots          = read_u32(buf, &off);
    m->qos_caught          = read_u32(buf, &off);
    m->contracts_fulfilled = read_u32(buf, &off);
    m->perfect_rounds      = read_u32(buf, &off);
    m->hearts_collected    = read_u32(buf, &off);
    m->tricks_won          = read_u32(buf, &off);
    m->best_score          = (int32_t)read_u32(buf, &off);
    m->worst_score         = (int32_t)read_u32(buf, &off);
    m->avg_placement_x100  = read_u16(buf, &off);
    return (int)off;
}

static int ser_leaderboard_request(const NetMsgLeaderboardRequest *m,
                                   uint8_t *buf, size_t len)
{
    if (len < NET_AUTH_TOKEN_LEN)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int deser_leaderboard_request(NetMsgLeaderboardRequest *m,
                                     const uint8_t *buf, size_t len)
{
    if (len < NET_AUTH_TOKEN_LEN)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int ser_leaderboard_response(const NetMsgLeaderboardResponse *m,
                                    uint8_t *buf, size_t len)
{
    /* 1 (count) + count * (32+4+4+4) + 2 (rank) + 4 (elo) */
    size_t need = 1 + (size_t)m->entry_count * (NET_MAX_NAME_LEN + 12) + 6;
    if (len < need)
        return -1;
    size_t off = 0;
    write_u8(buf, &off, m->entry_count);
    for (int i = 0; i < m->entry_count; i++) {
        const NetLeaderboardEntry *e = &m->entries[i];
        write_bytes(buf, &off, e->username, NET_MAX_NAME_LEN);
        write_u32(buf, &off, (uint32_t)e->elo_rating);
        write_u32(buf, &off, e->games_played);
        write_u32(buf, &off, e->games_won);
    }
    write_u16(buf, &off, m->player_rank);
    write_u32(buf, &off, (uint32_t)m->player_elo);
    return (int)off;
}

static int deser_leaderboard_response(NetMsgLeaderboardResponse *m,
                                      const uint8_t *buf, size_t len)
{
    if (len < 1)
        return -1;
    size_t off = 0;
    m->entry_count = read_u8(buf, &off);
    if (m->entry_count > LEADERBOARD_MAX_ENTRIES)
        return -1;
    size_t need = 1 + (size_t)m->entry_count * (NET_MAX_NAME_LEN + 12) + 6;
    if (len < need)
        return -1;
    for (int i = 0; i < m->entry_count; i++) {
        NetLeaderboardEntry *e = &m->entries[i];
        read_bytes(buf, &off, e->username, NET_MAX_NAME_LEN);
        e->username[NET_MAX_NAME_LEN - 1] = '\0';
        e->elo_rating   = (int32_t)read_u32(buf, &off);
        e->games_played = read_u32(buf, &off);
        e->games_won    = read_u32(buf, &off);
    }
    m->player_rank = read_u16(buf, &off);
    m->player_elo  = (int32_t)read_u32(buf, &off);
    return (int)off;
}

/* --- Lobby <-> Server messages --- */

static int ser_server_register(const NetMsgServerRegister *m, uint8_t *buf,
                               size_t len)
{
    size_t need = NET_ADDR_LEN + 6;
    if (len < need)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->addr, NET_ADDR_LEN);
    write_u16(buf, &off, m->port);
    write_u16(buf, &off, m->max_rooms);
    write_u16(buf, &off, m->current_rooms);
    return (int)off;
}

static int deser_server_register(NetMsgServerRegister *m, const uint8_t *buf,
                                 size_t len)
{
    size_t need = NET_ADDR_LEN + 6;
    if (len < need)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->addr, NET_ADDR_LEN);
    m->port = read_u16(buf, &off);
    m->max_rooms = read_u16(buf, &off);
    m->current_rooms = read_u16(buf, &off);
    return (int)off;
}

static int ser_server_create_room(const NetMsgServerCreateRoom *m,
                                  uint8_t *buf, size_t len)
{
    size_t need =
        NET_ROOM_CODE_LEN + (NET_MAX_PLAYERS * NET_AUTH_TOKEN_LEN);
    if (len < need)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_bytes(buf, &off, m->player_tokens[i], NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int deser_server_create_room(NetMsgServerCreateRoom *m,
                                    const uint8_t *buf, size_t len)
{
    size_t need =
        NET_ROOM_CODE_LEN + (NET_MAX_PLAYERS * NET_AUTH_TOKEN_LEN);
    if (len < need)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    m->room_code[NET_ROOM_CODE_LEN - 1] = '\0';
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        read_bytes(buf, &off, m->player_tokens[i], NET_AUTH_TOKEN_LEN);
    return (int)off;
}

static int ser_server_result(const NetMsgServerResult *m, uint8_t *buf,
                             size_t len)
{
    /* Base + 6 stat arrays * 4 players * 2 bytes each = +48 */
    size_t need = NET_ROOM_CODE_LEN + 8 + 4 + 1 + 2
                  + (NET_MAX_PLAYERS * NET_AUTH_TOKEN_LEN)
                  + (6 * NET_MAX_PLAYERS * 2);
    if (len < need)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_i16(buf, &off, m->final_scores[i]);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u8(buf, &off, m->winner_seats[i]);
    write_u8(buf, &off, m->winner_count);
    write_u16(buf, &off, m->rounds_played);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_bytes(buf, &off, m->player_tokens[i], NET_AUTH_TOKEN_LEN);
    /* Extended stat counters */
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u16(buf, &off, m->moon_shots[i]);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u16(buf, &off, m->qos_caught[i]);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u16(buf, &off, m->contracts_fulfilled[i]);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u16(buf, &off, m->perfect_rounds[i]);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u16(buf, &off, m->hearts_collected[i]);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u16(buf, &off, m->tricks_won[i]);
    return (int)off;
}

static int deser_server_result(NetMsgServerResult *m, const uint8_t *buf,
                               size_t len)
{
    size_t need = NET_ROOM_CODE_LEN + 8 + 4 + 1 + 2
                  + (NET_MAX_PLAYERS * NET_AUTH_TOKEN_LEN)
                  + (6 * NET_MAX_PLAYERS * 2);
    if (len < need)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    m->room_code[NET_ROOM_CODE_LEN - 1] = '\0';
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->final_scores[i] = read_i16(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->winner_seats[i] = read_u8(buf, &off);
    m->winner_count = read_u8(buf, &off);
    m->rounds_played = read_u16(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        read_bytes(buf, &off, m->player_tokens[i], NET_AUTH_TOKEN_LEN);
    /* Extended stat counters */
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->moon_shots[i] = read_u16(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->qos_caught[i] = read_u16(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->contracts_fulfilled[i] = read_u16(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->perfect_rounds[i] = read_u16(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->hearts_collected[i] = read_u16(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->tricks_won[i] = read_u16(buf, &off);
    return (int)off;
}

static int ser_server_heartbeat(const NetMsgServerHeartbeat *m, uint8_t *buf,
                                size_t len)
{
    if (len < 4)
        return -1;
    size_t off = 0;
    write_u16(buf, &off, m->current_rooms);
    write_u16(buf, &off, m->current_players);
    return (int)off;
}

static int deser_server_heartbeat(NetMsgServerHeartbeat *m, const uint8_t *buf,
                                  size_t len)
{
    if (len < 4)
        return -1;
    size_t off = 0;
    m->current_rooms = read_u16(buf, &off);
    m->current_players = read_u16(buf, &off);
    return (int)off;
}

static int ser_server_room_created(const NetMsgServerRoomCreated *m,
                                   uint8_t *buf, size_t len)
{
    size_t need = NET_ROOM_CODE_LEN + 1;
    if (len < need)
        return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    write_u8(buf, &off, m->success);
    return (int)off;
}

static int deser_server_room_created(NetMsgServerRoomCreated *m,
                                     const uint8_t *buf, size_t len)
{
    size_t need = NET_ROOM_CODE_LEN + 1;
    if (len < need)
        return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    m->room_code[NET_ROOM_CODE_LEN - 1] = '\0';
    m->success = read_u8(buf, &off);
    return (int)off;
}

static int ser_server_room_destroyed(const NetMsgServerRoomDestroyed *m,
                                     uint8_t *buf, size_t len)
{
    if (len < NET_ROOM_CODE_LEN) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    return (int)off;
}

static int deser_server_room_destroyed(NetMsgServerRoomDestroyed *m,
                                       const uint8_t *buf, size_t len)
{
    if (len < NET_ROOM_CODE_LEN) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    m->room_code[NET_ROOM_CODE_LEN - 1] = '\0';
    return (int)off;
}

static int ser_server_elo_result(const NetMsgServerEloResult *m,
                                uint8_t *buf, size_t len)
{
    size_t need = NET_ROOM_CODE_LEN + NET_MAX_PLAYERS * 4 * 2;
    if (len < need) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u32(buf, &off, (uint32_t)m->prev_elo[i]);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u32(buf, &off, (uint32_t)m->new_elo[i]);
    return (int)off;
}

static int deser_server_elo_result(NetMsgServerEloResult *m,
                                   const uint8_t *buf, size_t len)
{
    size_t need = NET_ROOM_CODE_LEN + NET_MAX_PLAYERS * 4 * 2;
    if (len < need) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->room_code, NET_ROOM_CODE_LEN);
    m->room_code[NET_ROOM_CODE_LEN - 1] = '\0';
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->prev_elo[i] = (int32_t)read_u32(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        m->new_elo[i] = (int32_t)read_u32(buf, &off);
    return (int)off;
}

static int ser_pass_confirmed(const NetMsgPassConfirmed *m,
                              uint8_t *buf, size_t len)
{
    if (len < 1) return -1;
    size_t off = 0;
    write_u8(buf, &off, m->seat);
    return (int)off;
}

static int deser_pass_confirmed(NetMsgPassConfirmed *m,
                                const uint8_t *buf, size_t len)
{
    if (len < 1) return -1;
    size_t off = 0;
    m->seat = read_u8(buf, &off);
    return (int)off;
}

/* ================================================================
 * Friend System Serialization Helpers
 * ================================================================ */

static int ser_friend_search(const NetMsgFriendSearch *m, uint8_t *buf,
                             size_t cap)
{
    if (cap < 64) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, 32);
    write_bytes(buf, &off, m->query, 32);
    return (int)off;
}
static int deser_friend_search(NetMsgFriendSearch *m, const uint8_t *buf,
                               size_t len)
{
    if (len < 64) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, 32);
    read_bytes(buf, &off, m->query, 32);
    m->query[31] = '\0';
    return (int)off;
}

static int ser_friend_search_result(const NetMsgFriendSearchResult *m,
                                    uint8_t *buf, size_t cap)
{
    size_t need = 1 + (size_t)m->count * 37;
    if (cap < need) return -1;
    size_t off = 0;
    write_u8(buf, &off, m->count);
    for (int i = 0; i < m->count; i++) {
        write_bytes(buf, &off, m->results[i].username, 32);
        write_u32(buf, &off, (uint32_t)m->results[i].account_id);
        write_u8(buf, &off, m->results[i].status);
    }
    return (int)off;
}
static int deser_friend_search_result(NetMsgFriendSearchResult *m,
                                      const uint8_t *buf, size_t len)
{
    if (len < 1) return -1;
    size_t off = 0;
    m->count = read_u8(buf, &off);
    if (m->count > 10) m->count = 10;
    for (int i = 0; i < m->count; i++) {
        if (off + 37 > len) return -1;
        read_bytes(buf, &off, m->results[i].username, 32);
        m->results[i].username[31] = '\0';
        m->results[i].account_id = (int32_t)read_u32(buf, &off);
        m->results[i].status = read_u8(buf, &off);
    }
    return (int)off;
}

static int ser_friend_request(const NetMsgFriendRequest *m, uint8_t *buf,
                              size_t cap)
{
    if (cap < 36) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, 32);
    write_u32(buf, &off, (uint32_t)m->target_account_id);
    return (int)off;
}
static int deser_friend_request(NetMsgFriendRequest *m, const uint8_t *buf,
                                size_t len)
{
    if (len < 36) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, 32);
    m->target_account_id = (int32_t)read_u32(buf, &off);
    return (int)off;
}

static int ser_friend_accept(const NetMsgFriendAccept *m, uint8_t *buf,
                             size_t cap)
{
    if (cap < 36) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, 32);
    write_u32(buf, &off, (uint32_t)m->from_account_id);
    return (int)off;
}
static int deser_friend_accept(NetMsgFriendAccept *m, const uint8_t *buf,
                               size_t len)
{
    if (len < 36) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, 32);
    m->from_account_id = (int32_t)read_u32(buf, &off);
    return (int)off;
}

static int ser_friend_reject(const NetMsgFriendReject *m, uint8_t *buf,
                             size_t cap)
{
    if (cap < 36) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, 32);
    write_u32(buf, &off, (uint32_t)m->from_account_id);
    return (int)off;
}
static int deser_friend_reject(NetMsgFriendReject *m, const uint8_t *buf,
                               size_t len)
{
    if (len < 36) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, 32);
    m->from_account_id = (int32_t)read_u32(buf, &off);
    return (int)off;
}

static int ser_friend_remove(const NetMsgFriendRemove *m, uint8_t *buf,
                             size_t cap)
{
    if (cap < 36) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, 32);
    write_u32(buf, &off, (uint32_t)m->target_account_id);
    return (int)off;
}
static int deser_friend_remove(NetMsgFriendRemove *m, const uint8_t *buf,
                               size_t len)
{
    if (len < 36) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, 32);
    m->target_account_id = (int32_t)read_u32(buf, &off);
    return (int)off;
}

static int ser_friend_list_request(const NetMsgFriendListRequest *m,
                                   uint8_t *buf, size_t cap)
{
    if (cap < 32) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, 32);
    return (int)off;
}
static int deser_friend_list_request(NetMsgFriendListRequest *m,
                                     const uint8_t *buf, size_t len)
{
    if (len < 32) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, 32);
    return (int)off;
}

static int ser_friend_list(const NetMsgFriendList *m, uint8_t *buf, size_t cap)
{
    size_t need = 1 + (size_t)m->friend_count * 37 +
                  1 + (size_t)m->request_count * 36;
    if (cap < need) return -1;
    size_t off = 0;
    write_u8(buf, &off, m->friend_count);
    for (int i = 0; i < m->friend_count; i++) {
        write_bytes(buf, &off, m->friends[i].username, 32);
        write_u32(buf, &off, (uint32_t)m->friends[i].account_id);
        write_u8(buf, &off, m->friends[i].presence);
    }
    write_u8(buf, &off, m->request_count);
    for (int i = 0; i < m->request_count; i++) {
        write_bytes(buf, &off, m->incoming_requests[i].username, 32);
        write_u32(buf, &off, (uint32_t)m->incoming_requests[i].account_id);
    }
    return (int)off;
}
static int deser_friend_list(NetMsgFriendList *m, const uint8_t *buf,
                             size_t len)
{
    if (len < 2) return -1;
    size_t off = 0;
    m->friend_count = read_u8(buf, &off);
    if (m->friend_count > 20) m->friend_count = 20;
    for (int i = 0; i < m->friend_count; i++) {
        if (off + 37 > len) return -1;
        read_bytes(buf, &off, m->friends[i].username, 32);
        m->friends[i].username[31] = '\0';
        m->friends[i].account_id = (int32_t)read_u32(buf, &off);
        m->friends[i].presence = read_u8(buf, &off);
    }
    if (off >= len) return -1;
    m->request_count = read_u8(buf, &off);
    if (m->request_count > 10) m->request_count = 10;
    for (int i = 0; i < m->request_count; i++) {
        if (off + 36 > len) return -1;
        read_bytes(buf, &off, m->incoming_requests[i].username, 32);
        m->incoming_requests[i].username[31] = '\0';
        m->incoming_requests[i].account_id = (int32_t)read_u32(buf, &off);
    }
    return (int)off;
}

static int ser_friend_update(const NetMsgFriendUpdate *m, uint8_t *buf,
                             size_t cap)
{
    if (cap < 5) return -1;
    size_t off = 0;
    write_u32(buf, &off, (uint32_t)m->account_id);
    write_u8(buf, &off, m->presence);
    return (int)off;
}
static int deser_friend_update(NetMsgFriendUpdate *m, const uint8_t *buf,
                               size_t len)
{
    if (len < 5) return -1;
    size_t off = 0;
    m->account_id = (int32_t)read_u32(buf, &off);
    m->presence = read_u8(buf, &off);
    return (int)off;
}

static int ser_friend_request_notify(const NetMsgFriendRequestNotify *m,
                                     uint8_t *buf, size_t cap)
{
    if (cap < 36) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->username, 32);
    write_u32(buf, &off, (uint32_t)m->account_id);
    return (int)off;
}
static int deser_friend_request_notify(NetMsgFriendRequestNotify *m,
                                       const uint8_t *buf, size_t len)
{
    if (len < 36) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->username, 32);
    m->username[31] = '\0';
    m->account_id = (int32_t)read_u32(buf, &off);
    return (int)off;
}

static int ser_room_invite(const NetMsgRoomInvite *m, uint8_t *buf, size_t cap)
{
    if (cap < 44) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->auth_token, 32);
    write_u32(buf, &off, (uint32_t)m->target_account_id);
    write_bytes(buf, &off, m->room_code, 8);
    return (int)off;
}
static int deser_room_invite(NetMsgRoomInvite *m, const uint8_t *buf,
                             size_t len)
{
    if (len < 44) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->auth_token, 32);
    m->target_account_id = (int32_t)read_u32(buf, &off);
    read_bytes(buf, &off, m->room_code, 8);
    m->room_code[7] = '\0';
    return (int)off;
}

static int ser_room_invite_notify(const NetMsgRoomInviteNotify *m,
                                  uint8_t *buf, size_t cap)
{
    if (cap < 40) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->from_username, 32);
    write_bytes(buf, &off, m->room_code, 8);
    return (int)off;
}
static int deser_room_invite_notify(NetMsgRoomInviteNotify *m,
                                    const uint8_t *buf, size_t len)
{
    if (len < 40) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->from_username, 32);
    m->from_username[31] = '\0';
    read_bytes(buf, &off, m->room_code, 8);
    m->room_code[7] = '\0';
    return (int)off;
}

static int ser_room_invite_expired(const NetMsgRoomInviteExpired *m,
                                   uint8_t *buf, size_t cap)
{
    if (cap < 8) return -1;
    size_t off = 0;
    write_bytes(buf, &off, m->room_code, 8);
    return (int)off;
}
static int deser_room_invite_expired(NetMsgRoomInviteExpired *m,
                                     const uint8_t *buf, size_t len)
{
    if (len < 8) return -1;
    size_t off = 0;
    read_bytes(buf, &off, m->room_code, 8);
    m->room_code[7] = '\0';
    return (int)off;
}

/* --- NetPlayerView serialization (the big one) --- */

static void ser_trick_view(uint8_t *buf, size_t *off, const NetTrickView *t)
{
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        write_card(buf, off, t->cards[i]);
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        write_u8(buf, off, t->player_ids[i]);
    write_u8(buf, off, t->lead_player);
    write_u8(buf, off, t->lead_suit);
    write_u8(buf, off, t->num_played);
}

/* Fixed wire sizes for sub-struct bounds checking */
#define TRICK_VIEW_BYTES    (CARDS_PER_TRICK * 2 + CARDS_PER_TRICK + 3)
#define CONTRACT_VIEW_BYTES (2 + 3 + 2 + 2 + SUIT_COUNT * 2 + 2 + 2 + 2)
#define TRANSMUTE_SLOT_BYTES (2 + 2 + 1 + 1 + 1)
#define DRAFT_PAIR_BYTES    (2 + 2)
#define DRAFT_PLAYER_BYTES  (NET_DRAFT_GROUP_SIZE * DRAFT_PAIR_BYTES + 1 + \
                             NET_DRAFT_PICKS * DRAFT_PAIR_BYTES + 1 + 1)
#define EFFECT_VIEW_BYTES   (1 + 2 + 1 + 1 + 1 + 2 + 1)
#define TRICK_TRANSMUTE_BYTES (CARDS_PER_TRICK * (2 + 1 + 1 + 1 + 1))
#define OPPONENT_CONTRACTS_BYTES (1 + NET_MAX_CONTRACTS * (1 + 1 + 2))

static bool deser_trick_view(const uint8_t *buf, size_t *off, size_t len,
                             NetTrickView *t)
{
    if (*off + TRICK_VIEW_BYTES > len) return false;
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        t->cards[i] = read_card(buf, off);
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        t->player_ids[i] = read_u8(buf, off);
    t->lead_player = read_u8(buf, off);
    t->lead_suit = read_u8(buf, off);
    t->num_played = read_u8(buf, off);
    return true;
}

static void ser_contract_view(uint8_t *buf, size_t *off,
                              const NetContractView *c)
{
    write_i16(buf, off, c->contract_id);
    write_bool(buf, off, c->revealed);
    write_bool(buf, off, c->completed);
    write_bool(buf, off, c->failed);
    write_i16(buf, off, c->tricks_won);
    write_i16(buf, off, c->points_taken);
    for (int i = 0; i < SUIT_COUNT; i++)
        write_i16(buf, off, c->cards_collected[i]);
    write_u16(buf, off, c->tricks_won_mask);
    write_i16(buf, off, c->max_streak);
    write_i16(buf, off, c->paired_transmutation_id);
}

static bool deser_contract_view(const uint8_t *buf, size_t *off, size_t len,
                                NetContractView *c)
{
    if (*off + CONTRACT_VIEW_BYTES > len) return false;
    c->contract_id = read_i16(buf, off);
    c->revealed = read_bool(buf, off);
    c->completed = read_bool(buf, off);
    c->failed = read_bool(buf, off);
    c->tricks_won = read_i16(buf, off);
    c->points_taken = read_i16(buf, off);
    for (int i = 0; i < SUIT_COUNT; i++)
        c->cards_collected[i] = read_i16(buf, off);
    c->tricks_won_mask = read_u16(buf, off);
    c->max_streak = read_i16(buf, off);
    c->paired_transmutation_id = read_i16(buf, off);
    return true;
}

static void ser_transmute_slot(uint8_t *buf, size_t *off,
                               const NetTransmuteSlotView *s)
{
    write_i16(buf, off, s->transmutation_id);
    write_card(buf, off, s->original_card);
    write_i8(buf, off, s->transmuter_player);
    write_bool(buf, off, s->fogged);
    write_i8(buf, off, s->fog_transmuter);
}

static bool deser_transmute_slot(const uint8_t *buf, size_t *off, size_t len,
                                 NetTransmuteSlotView *s)
{
    if (*off + TRANSMUTE_SLOT_BYTES > len) return false;
    s->transmutation_id = read_i16(buf, off);
    s->original_card = read_card(buf, off);
    s->transmuter_player = read_i8(buf, off);
    s->fogged = read_bool(buf, off);
    s->fog_transmuter = read_i8(buf, off);
    return true;
}

static void ser_draft_pair(uint8_t *buf, size_t *off, const NetDraftPair *d)
{
    write_i16(buf, off, d->contract_id);
    write_i16(buf, off, d->transmutation_id);
}

static bool deser_draft_pair(const uint8_t *buf, size_t *off, size_t len,
                             NetDraftPair *d)
{
    if (*off + DRAFT_PAIR_BYTES > len) return false;
    d->contract_id = read_i16(buf, off);
    d->transmutation_id = read_i16(buf, off);
    return true;
}

static void ser_draft_player(uint8_t *buf, size_t *off,
                             const NetDraftPlayerView *d)
{
    for (int i = 0; i < NET_DRAFT_GROUP_SIZE; i++)
        ser_draft_pair(buf, off, &d->available[i]);
    write_u8(buf, off, d->available_count);
    for (int i = 0; i < NET_DRAFT_PICKS; i++)
        ser_draft_pair(buf, off, &d->picked[i]);
    write_u8(buf, off, d->pick_count);
    write_bool(buf, off, d->has_picked_this_round);
}

static bool deser_draft_player(const uint8_t *buf, size_t *off, size_t len,
                               NetDraftPlayerView *d)
{
    if (*off + DRAFT_PLAYER_BYTES > len) return false;
    for (int i = 0; i < NET_DRAFT_GROUP_SIZE; i++)
        deser_draft_pair(buf, off, len, &d->available[i]);
    d->available_count = read_u8(buf, off);
    for (int i = 0; i < NET_DRAFT_PICKS; i++)
        deser_draft_pair(buf, off, len, &d->picked[i]);
    d->pick_count = read_u8(buf, off);
    d->has_picked_this_round = read_bool(buf, off);
    return true;
}

static void ser_effect_view(uint8_t *buf, size_t *off,
                            const NetActiveEffectView *e)
{
    write_u8(buf, off, e->effect_type);
    write_i16(buf, off, e->param_value);
    write_u8(buf, off, e->scope);
    write_i8(buf, off, e->source_player);
    write_i8(buf, off, e->target_player);
    write_i16(buf, off, e->rounds_remaining);
    write_bool(buf, off, e->active);
}

static bool deser_effect_view(const uint8_t *buf, size_t *off, size_t len,
                              NetActiveEffectView *e)
{
    if (*off + EFFECT_VIEW_BYTES > len) return false;
    e->effect_type = read_u8(buf, off);
    e->param_value = read_i16(buf, off);
    e->scope = read_u8(buf, off);
    e->source_player = read_i8(buf, off);
    e->target_player = read_i8(buf, off);
    e->rounds_remaining = read_i16(buf, off);
    e->active = read_bool(buf, off);
    return true;
}

static void ser_trick_transmute(uint8_t *buf, size_t *off,
                                const NetTrickTransmuteView *t)
{
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        write_i16(buf, off, t->transmutation_ids[i]);
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        write_i8(buf, off, t->transmuter_player[i]);
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        write_u8(buf, off, t->resolved_effects[i]);
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        write_bool(buf, off, t->fogged[i]);
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        write_i8(buf, off, t->fog_transmuter[i]);
}

static bool deser_trick_transmute(const uint8_t *buf, size_t *off, size_t len,
                                  NetTrickTransmuteView *t)
{
    if (*off + TRICK_TRANSMUTE_BYTES > len) return false;
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        t->transmutation_ids[i] = read_i16(buf, off);
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        t->transmuter_player[i] = read_i8(buf, off);
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        t->resolved_effects[i] = read_u8(buf, off);
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        t->fogged[i] = read_bool(buf, off);
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        t->fog_transmuter[i] = read_i8(buf, off);
    return true;
}

static void ser_opponent_contracts(uint8_t *buf, size_t *off,
                                   const NetOpponentContracts *oc)
{
    write_u8(buf, off, oc->num_contracts);
    for (int i = 0; i < NET_MAX_CONTRACTS; i++)
        write_bool(buf, off, oc->revealed[i]);
    for (int i = 0; i < NET_MAX_CONTRACTS; i++)
        write_bool(buf, off, oc->completed[i]);
    for (int i = 0; i < NET_MAX_CONTRACTS; i++)
        write_i16(buf, off, oc->contract_ids[i]);
}

static bool deser_opponent_contracts(const uint8_t *buf, size_t *off,
                                     size_t len, NetOpponentContracts *oc)
{
    if (*off + OPPONENT_CONTRACTS_BYTES > len) return false;
    oc->num_contracts = read_u8(buf, off);
    for (int i = 0; i < NET_MAX_CONTRACTS; i++)
        oc->revealed[i] = read_bool(buf, off);
    for (int i = 0; i < NET_MAX_CONTRACTS; i++)
        oc->completed[i] = read_bool(buf, off);
    for (int i = 0; i < NET_MAX_CONTRACTS; i++)
        oc->contract_ids[i] = read_i16(buf, off);
    return true;
}

static int ser_player_view(const NetPlayerView *v, uint8_t *buf, size_t len)
{
    if (len < NET_PLAYER_VIEW_MAX_SIZE)
        return -1;

    /* Clamp counts to prevent out-of-bounds reads from source struct */
    uint8_t hc = v->hand_count <= NET_MAX_HAND_SIZE
                     ? v->hand_count : NET_MAX_HAND_SIZE;

    size_t off = 0;

    /* Identity + phase */
    write_u8(buf, &off, v->my_seat);
    write_u8(buf, &off, v->phase);
    write_u8(buf, &off, v->flow_step);
    write_u8(buf, &off, v->pass_subphase);

    /* Round info */
    write_u16(buf, &off, v->round_number);
    write_u8(buf, &off, v->pass_direction);
    write_u8(buf, &off, v->pass_card_count);
    write_u8(buf, &off, v->lead_player);
    write_bool(buf, &off, v->hearts_broken);
    write_u8(buf, &off, v->tricks_played);

    /* Own hand (use clamped count) */
    write_u8(buf, &off, hc);
    for (int i = 0; i < hc; i++)
        write_card(buf, &off, v->hand[i]);

    /* Hand counts */
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_u8(buf, &off, v->hand_counts[i]);

    /* Scores */
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_i16(buf, &off, v->round_points[i]);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_i16(buf, &off, v->total_scores[i]);

    /* Trick */
    ser_trick_view(buf, &off, &v->current_trick);

    /* Pass state */
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        write_bool(buf, &off, v->pass_ready[i]);
    for (int i = 0; i < NET_MAX_PASS_CARDS; i++)
        write_card(buf, &off, v->my_pass_selections[i]);

    /* Dealer / Turn */
    write_i8(buf, &off, v->dealer_seat);
    write_i8(buf, &off, v->current_turn_player);
    write_f32(buf, &off, v->turn_timer);
    write_f32(buf, &off, v->turn_time_limit);

    /* Phase 2 */
    write_bool(buf, &off, v->phase2_enabled);
    if (v->phase2_enabled) {
        /* Own contracts (clamped) */
        uint8_t nc = v->my_num_contracts <= NET_MAX_CONTRACTS
                         ? v->my_num_contracts : NET_MAX_CONTRACTS;
        write_u8(buf, &off, nc);
        for (int i = 0; i < nc; i++)
            ser_contract_view(buf, &off, &v->my_contracts[i]);

        /* Opponent contracts */
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            ser_opponent_contracts(buf, &off, &v->opponent_contracts[i]);

        /* Transmute inventory (clamped) */
        uint8_t ic = v->transmute_inv_count <= NET_MAX_TRANSMUTE_INV
                         ? v->transmute_inv_count : NET_MAX_TRANSMUTE_INV;
        write_u8(buf, &off, ic);
        for (int i = 0; i < ic; i++)
            write_i16(buf, &off, v->transmute_inv[i]);

        /* Hand transmute state (uses clamped hand count) */
        for (int i = 0; i < hc; i++)
            ser_transmute_slot(buf, &off, &v->hand_transmutes[i]);

        /* Draft */
        ser_draft_player(buf, &off, &v->my_draft);
        write_u8(buf, &off, v->draft_current_round);
        write_bool(buf, &off, v->draft_active);

        /* Trick transmutes */
        ser_trick_transmute(buf, &off, &v->trick_transmutes);

        /* Persistent effects (clamped) */
        uint8_t ec = v->num_persistent_effects <=
                             NET_MAX_PLAYERS * NET_MAX_EFFECTS
                         ? v->num_persistent_effects
                         : NET_MAX_PLAYERS * NET_MAX_EFFECTS;
        write_u8(buf, &off, ec);
        for (int i = 0; i < ec; i++)
            ser_effect_view(buf, &off, &v->persistent_effects[i]);

        /* Game-scoped arrays */
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            write_i8(buf, &off, v->shield_tricks_remaining[i]);
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            write_bool(buf, &off, v->curse_force_hearts[i]);
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            write_i8(buf, &off, v->anchor_force_suit[i]);
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            write_i8(buf, &off, v->binding_auto_win[i]);

        /* Prev round points */
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            write_i16(buf, &off, v->prev_round_points[i]);

        /* Mirror history */
        write_i16(buf, &off, v->last_played_transmute_id);
        write_u8(buf, &off, v->last_played_resolved_effect);
        write_i8(buf, &off, v->last_played_transmuted_card_suit);
        write_i8(buf, &off, v->last_played_transmuted_card_rank);

        /* Server-authoritative trick winner */
        write_i8(buf, &off, v->trick_winner);

        /* Rogue/Duel pending effect winners */
        write_i8(buf, &off, v->rogue_pending_winner);
        write_i8(buf, &off, v->duel_pending_winner);

        /* Rogue suit reveal */
        write_i8(buf, &off, v->rogue_chosen_suit);
        write_i8(buf, &off, v->rogue_chosen_target);
        write_i8(buf, &off, v->rogue_revealed_count);
        {
            int rc = v->rogue_revealed_count > 0 ? v->rogue_revealed_count : 0;
            for (int i = 0; i < rc; i++)
                write_card(buf, &off, v->rogue_revealed_cards[i]);
        }

        /* Duel */
        write_i8(buf, &off, v->duel_chosen_card_idx);
        write_i8(buf, &off, v->duel_chosen_target);
        write_card(buf, &off, v->duel_revealed_card);
        write_i8(buf, &off, v->duel_was_swap);

        /* Round-end transmutation effect tracking */
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            write_i8(buf, &off, v->martyr_flags[i]);
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            write_i8(buf, &off, v->gatherer_reduction[i]);
    }

    return (int)off;
}

static int deser_player_view(NetPlayerView *v, const uint8_t *buf, size_t len)
{
    /* Minimum: base fields before phase2 need ~80 bytes */
    if (len < 80)
        return -1;

    memset(v, 0, sizeof(*v));
    size_t off = 0;

    v->my_seat = read_u8(buf, &off);
    v->phase = read_u8(buf, &off);
    v->flow_step = read_u8(buf, &off);
    v->pass_subphase = read_u8(buf, &off);

    v->round_number = read_u16(buf, &off);
    v->pass_direction = read_u8(buf, &off);
    v->pass_card_count = read_u8(buf, &off);
    v->lead_player = read_u8(buf, &off);
    v->hearts_broken = read_bool(buf, &off);
    v->tricks_played = read_u8(buf, &off);

    v->hand_count = read_u8(buf, &off);
    if (v->hand_count > NET_MAX_HAND_SIZE)
        return -1;
    if (off + (size_t)v->hand_count * 2 > len)
        return -1;
    for (int i = 0; i < v->hand_count; i++)
        v->hand[i] = read_card(buf, &off);

    /* hand_counts(4) + round_points(4*2) + total_scores(4*2) = 20 bytes */
    if (off + NET_MAX_PLAYERS + NET_MAX_PLAYERS * 2 + NET_MAX_PLAYERS * 2 > len)
        return -1;
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        v->hand_counts[i] = read_u8(buf, &off);

    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        v->round_points[i] = read_i16(buf, &off);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        v->total_scores[i] = read_i16(buf, &off);

    if (!deser_trick_view(buf, &off, len, &v->current_trick))
        return -1;

    /* pass_ready(4) + pass_selections(4*2) + dealer(1) + turn(1) + timer(4) = 17 */
    if (off + NET_MAX_PLAYERS + NET_MAX_PASS_CARDS * 2 + 1 + 1 + 4 > len)
        return -1;
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        v->pass_ready[i] = read_bool(buf, &off);
    for (int i = 0; i < NET_MAX_PASS_CARDS; i++)
        v->my_pass_selections[i] = read_card(buf, &off);

    v->dealer_seat = read_i8(buf, &off);
    v->current_turn_player = read_i8(buf, &off);
    v->turn_timer = read_f32(buf, &off);
    v->turn_time_limit = read_f32(buf, &off);

    if (off >= len)
        return -1;
    v->phase2_enabled = read_bool(buf, &off);
    if (v->phase2_enabled) {
        if (off >= len)
            return -1;
        v->my_num_contracts = read_u8(buf, &off);
        if (v->my_num_contracts > NET_MAX_CONTRACTS)
            return -1;
        for (int i = 0; i < v->my_num_contracts; i++) {
            if (!deser_contract_view(buf, &off, len, &v->my_contracts[i]))
                return -1;
        }

        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            if (!deser_opponent_contracts(buf, &off, len,
                                          &v->opponent_contracts[i]))
                return -1;
        }

        if (off >= len)
            return -1;
        v->transmute_inv_count = read_u8(buf, &off);
        if (v->transmute_inv_count > NET_MAX_TRANSMUTE_INV)
            return -1;
        if (off + (size_t)v->transmute_inv_count * 2 > len)
            return -1;
        for (int i = 0; i < v->transmute_inv_count; i++)
            v->transmute_inv[i] = read_i16(buf, &off);

        for (int i = 0; i < v->hand_count; i++) {
            if (!deser_transmute_slot(buf, &off, len, &v->hand_transmutes[i]))
                return -1;
        }

        if (!deser_draft_player(buf, &off, len, &v->my_draft))
            return -1;
        if (off + 2 > len) /* draft_current_round(1) + draft_active(1) */
            return -1;
        v->draft_current_round = read_u8(buf, &off);
        v->draft_active = read_bool(buf, &off);

        if (!deser_trick_transmute(buf, &off, len, &v->trick_transmutes))
            return -1;

        if (off >= len)
            return -1;
        v->num_persistent_effects = read_u8(buf, &off);
        if (v->num_persistent_effects > NET_MAX_PLAYERS * NET_MAX_EFFECTS)
            return -1;
        for (int i = 0; i < v->num_persistent_effects; i++) {
            if (!deser_effect_view(buf, &off, len,
                                   &v->persistent_effects[i]))
                return -1;
        }

        /* Game-scoped arrays through duel_pending = 30 fixed bytes min;
         * rogue revealed cards are variable-length, checked inline */
        if (off + 30 > len)
            return -1;
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            v->shield_tricks_remaining[i] = read_i8(buf, &off);
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            v->curse_force_hearts[i] = read_bool(buf, &off);
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            v->anchor_force_suit[i] = read_i8(buf, &off);
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            v->binding_auto_win[i] = read_i8(buf, &off);

        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            v->prev_round_points[i] = read_i16(buf, &off);

        /* Mirror history */
        v->last_played_transmute_id = read_i16(buf, &off);
        v->last_played_resolved_effect = read_u8(buf, &off);
        v->last_played_transmuted_card_suit = read_i8(buf, &off);
        v->last_played_transmuted_card_rank = read_i8(buf, &off);

        /* Server-authoritative trick winner */
        v->trick_winner = read_i8(buf, &off);

        /* Rogue/Duel pending effect winners */
        v->rogue_pending_winner = read_i8(buf, &off);
        v->duel_pending_winner = read_i8(buf, &off);

        /* Rogue suit reveal */
        v->rogue_chosen_suit = read_i8(buf, &off);
        v->rogue_chosen_target = read_i8(buf, &off);
        v->rogue_revealed_count = read_i8(buf, &off);
        {
            int rc = v->rogue_revealed_count > 0 ? v->rogue_revealed_count : 0;
            if (rc > NET_MAX_HAND_SIZE) return -1;
            if (off + (size_t)rc * 2 > len) return -1;
            for (int i = 0; i < rc; i++)
                v->rogue_revealed_cards[i] = read_card(buf, &off);
        }

        /* Duel */
        v->duel_chosen_card_idx = read_i8(buf, &off);
        v->duel_chosen_target = read_i8(buf, &off);
        v->duel_revealed_card = read_card(buf, &off);
        v->duel_was_swap = read_i8(buf, &off);

        /* Round-end transmutation effect tracking */
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            v->martyr_flags[i] = read_i8(buf, &off);
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            v->gatherer_reduction[i] = read_i8(buf, &off);
    }

    return (int)off;
}

/* ================================================================
 * Top-Level Serialize / Deserialize Dispatch
 * ================================================================ */

int net_msg_serialize(const NetMsg *msg, uint8_t *buf, size_t buf_size)
{
    if (buf_size < 1)
        return -1;

    /* First byte is always the message type */
    size_t off = 0;
    write_u8(buf, &off, (uint8_t)msg->type);

    uint8_t *payload = buf + off;
    size_t remaining = buf_size - off;
    int n = 0;

    switch (msg->type) {
    /* Connection */
    case NET_MSG_HANDSHAKE:
        n = ser_handshake(&msg->handshake, payload, remaining);
        break;
    case NET_MSG_HANDSHAKE_ACK:
        n = ser_handshake_ack(&msg->handshake_ack, payload, remaining);
        break;
    case NET_MSG_HANDSHAKE_REJECT:
        n = ser_handshake_reject(&msg->handshake_reject, payload, remaining);
        break;
    case NET_MSG_PING:
        n = ser_ping(&msg->ping, payload, remaining);
        break;
    case NET_MSG_PONG:
        n = ser_pong(&msg->pong, payload, remaining);
        break;
    case NET_MSG_DISCONNECT:
        n = ser_disconnect(&msg->disconnect, payload, remaining);
        break;
    case NET_MSG_ERROR:
        n = ser_error(&msg->error, payload, remaining);
        break;
    case NET_MSG_CHAT:
        n = ser_chat(&msg->chat, payload, remaining);
        break;
    case NET_MSG_ROOM_STATUS:
        n = ser_room_status(&msg->room_status, payload, remaining);
        break;
    case NET_MSG_REQUEST_ADD_AI:
        n = 0; /* no payload */
        break;
    case NET_MSG_REQUEST_REMOVE_AI:
        n = 0; /* no payload */
        break;
    case NET_MSG_REQUEST_START_GAME:
        if (remaining < 4) return -1;
        payload[0] = msg->start_game.ai_difficulty;
        payload[1] = msg->start_game.timer_option;
        payload[2] = msg->start_game.point_goal_idx;
        payload[3] = msg->start_game.gamemode;
        n = 4;
        break;

    /* Game */
    case NET_MSG_INPUT_CMD:
        n = ser_input_cmd(&msg->input_cmd, payload, remaining);
        break;
    case NET_MSG_STATE_UPDATE:
        n = ser_player_view(&msg->state_update, payload, remaining);
        break;
    case NET_MSG_ROUND_START:
        n = ser_round_start(&msg->round_start, payload, remaining);
        break;
    case NET_MSG_TRICK_RESULT:
        n = ser_trick_result(&msg->trick_result, payload, remaining);
        break;
    case NET_MSG_GAME_OVER:
        n = ser_game_over(&msg->game_over, payload, remaining);
        break;
    case NET_MSG_PHASE_CHANGE:
        n = ser_phase_change(&msg->phase_change, payload, remaining);
        break;

    /* Lobby */
    case NET_MSG_REGISTER:
        n = ser_register(&msg->reg, payload, remaining);
        break;
    case NET_MSG_LOGIN:
        n = ser_login(&msg->login, payload, remaining);
        break;
    case NET_MSG_LOGIN_ACK:
        n = ser_login_ack(&msg->login_ack, payload, remaining);
        break;
    case NET_MSG_LOGIN_CHALLENGE:
        n = ser_login_challenge(&msg->login_challenge, payload, remaining);
        break;
    case NET_MSG_LOGIN_RESPONSE:
        n = ser_login_response(&msg->login_response, payload, remaining);
        break;
    case NET_MSG_LOGOUT:
    case NET_MSG_QUEUE_CANCEL:
    case NET_MSG_REGISTER_ACK:
        n = 0; /* no payload */
        break;
    case NET_MSG_CREATE_ROOM:
        n = ser_create_room(&msg->create_room, payload, remaining);
        break;
    case NET_MSG_JOIN_ROOM:
        n = ser_join_room(&msg->join_room, payload, remaining);
        break;
    case NET_MSG_ROOM_ASSIGNED:
        n = ser_room_assigned(&msg->room_assigned, payload, remaining);
        break;
    case NET_MSG_QUEUE_MATCHMAKE:
        n = ser_queue_matchmake(&msg->queue_matchmake, payload, remaining);
        break;
    case NET_MSG_QUEUE_STATUS:
        n = ser_queue_status(&msg->queue_status, payload, remaining);
        break;
    case NET_MSG_CHANGE_USERNAME:
        n = ser_change_username(&msg->change_username, payload, remaining);
        break;
    case NET_MSG_STATS_REQUEST:
        n = ser_stats_request(&msg->stats_request, payload, remaining);
        break;
    case NET_MSG_STATS_RESPONSE:
        n = ser_stats_response(&msg->stats_response, payload, remaining);
        break;
    case NET_MSG_LEADERBOARD_REQUEST:
        n = ser_leaderboard_request(&msg->leaderboard_request, payload,
                                    remaining);
        break;
    case NET_MSG_LEADERBOARD_RESPONSE:
        n = ser_leaderboard_response(&msg->leaderboard_response, payload,
                                     remaining);
        break;

    /* Lobby <-> Server */
    case NET_MSG_SERVER_REGISTER:
        n = ser_server_register(&msg->server_register, payload, remaining);
        break;
    case NET_MSG_SERVER_CREATE_ROOM:
        n = ser_server_create_room(&msg->server_create_room, payload,
                                   remaining);
        break;
    case NET_MSG_SERVER_RESULT:
        n = ser_server_result(&msg->server_result, payload, remaining);
        break;
    case NET_MSG_SERVER_HEARTBEAT:
        n = ser_server_heartbeat(&msg->server_heartbeat, payload, remaining);
        break;
    case NET_MSG_SERVER_ROOM_CREATED:
        n = ser_server_room_created(&msg->server_room_created, payload,
                                    remaining);
        break;
    case NET_MSG_SERVER_ROOM_DESTROYED:
        n = ser_server_room_destroyed(&msg->server_room_destroyed, payload,
                                      remaining);
        break;
    case NET_MSG_SERVER_ELO_RESULT:
        n = ser_server_elo_result(&msg->server_elo_result, payload, remaining);
        break;
    case NET_MSG_PASS_CONFIRMED:
        n = ser_pass_confirmed(&msg->pass_confirmed, payload, remaining);
        break;
    case NET_MSG_FRIEND_SEARCH:
        n = ser_friend_search(&msg->friend_search, payload, remaining); break;
    case NET_MSG_FRIEND_SEARCH_RESULT:
        n = ser_friend_search_result(&msg->friend_search_result, payload, remaining); break;
    case NET_MSG_FRIEND_REQUEST:
        n = ser_friend_request(&msg->friend_request, payload, remaining); break;
    case NET_MSG_FRIEND_ACCEPT:
        n = ser_friend_accept(&msg->friend_accept, payload, remaining); break;
    case NET_MSG_FRIEND_REJECT:
        n = ser_friend_reject(&msg->friend_reject, payload, remaining); break;
    case NET_MSG_FRIEND_REMOVE:
        n = ser_friend_remove(&msg->friend_remove, payload, remaining); break;
    case NET_MSG_FRIEND_LIST_REQUEST:
        n = ser_friend_list_request(&msg->friend_list_request, payload, remaining); break;
    case NET_MSG_FRIEND_LIST:
        n = ser_friend_list(&msg->friend_list, payload, remaining); break;
    case NET_MSG_FRIEND_UPDATE:
        n = ser_friend_update(&msg->friend_update, payload, remaining); break;
    case NET_MSG_FRIEND_REQUEST_NOTIFY:
        n = ser_friend_request_notify(&msg->friend_request_notify, payload, remaining); break;
    case NET_MSG_ROOM_INVITE:
        n = ser_room_invite(&msg->room_invite, payload, remaining); break;
    case NET_MSG_ROOM_INVITE_NOTIFY:
        n = ser_room_invite_notify(&msg->room_invite_notify, payload, remaining); break;
    case NET_MSG_ROOM_INVITE_EXPIRED:
        n = ser_room_invite_expired(&msg->room_invite_expired, payload, remaining); break;

    default:
        return -1;
    }

    if (n < 0)
        return -1;
    return (int)(off + (size_t)n);
}

int net_msg_deserialize(NetMsg *msg, const uint8_t *buf, size_t buf_len)
{
    if (buf_len < 1)
        return -1;

    memset(msg, 0, sizeof(*msg));
    size_t off = 0;
    msg->type = (NetMsgType)read_u8(buf, &off);

    const uint8_t *payload = buf + off;
    size_t remaining = buf_len - off;
    int n = 0;

    switch (msg->type) {
    case NET_MSG_HANDSHAKE:
        n = deser_handshake(&msg->handshake, payload, remaining);
        break;
    case NET_MSG_HANDSHAKE_ACK:
        n = deser_handshake_ack(&msg->handshake_ack, payload, remaining);
        break;
    case NET_MSG_HANDSHAKE_REJECT:
        n = deser_handshake_reject(&msg->handshake_reject, payload, remaining);
        break;
    case NET_MSG_PING:
        n = deser_ping(&msg->ping, payload, remaining);
        break;
    case NET_MSG_PONG:
        n = deser_pong(&msg->pong, payload, remaining);
        break;
    case NET_MSG_DISCONNECT:
        n = deser_disconnect(&msg->disconnect, payload, remaining);
        break;
    case NET_MSG_ERROR:
        n = deser_error(&msg->error, payload, remaining);
        break;
    case NET_MSG_CHAT:
        n = deser_chat(&msg->chat, payload, remaining);
        break;
    case NET_MSG_ROOM_STATUS:
        n = deser_room_status(&msg->room_status, payload, remaining);
        break;
    case NET_MSG_REQUEST_ADD_AI:
        n = 0; /* no payload */
        break;
    case NET_MSG_REQUEST_REMOVE_AI:
        n = 0; /* no payload */
        break;
    case NET_MSG_REQUEST_START_GAME:
        if (remaining < 4) return -1;
        msg->start_game.ai_difficulty  = payload[0];
        msg->start_game.timer_option   = payload[1];
        msg->start_game.point_goal_idx = payload[2];
        msg->start_game.gamemode       = payload[3];
        n = 4;
        break;
    case NET_MSG_INPUT_CMD:
        n = deser_input_cmd(&msg->input_cmd, payload, remaining);
        break;
    case NET_MSG_STATE_UPDATE:
        n = deser_player_view(&msg->state_update, payload, remaining);
        break;
    case NET_MSG_ROUND_START:
        n = deser_round_start(&msg->round_start, payload, remaining);
        break;
    case NET_MSG_TRICK_RESULT:
        n = deser_trick_result(&msg->trick_result, payload, remaining);
        break;
    case NET_MSG_GAME_OVER:
        n = deser_game_over(&msg->game_over, payload, remaining);
        break;
    case NET_MSG_PHASE_CHANGE:
        n = deser_phase_change(&msg->phase_change, payload, remaining);
        break;
    case NET_MSG_REGISTER:
        n = deser_register(&msg->reg, payload, remaining);
        break;
    case NET_MSG_LOGIN:
        n = deser_login(&msg->login, payload, remaining);
        break;
    case NET_MSG_LOGIN_ACK:
        n = deser_login_ack(&msg->login_ack, payload, remaining);
        break;
    case NET_MSG_LOGIN_CHALLENGE:
        n = deser_login_challenge(&msg->login_challenge, payload, remaining);
        break;
    case NET_MSG_LOGIN_RESPONSE:
        n = deser_login_response(&msg->login_response, payload, remaining);
        break;
    case NET_MSG_LOGOUT:
    case NET_MSG_QUEUE_CANCEL:
    case NET_MSG_REGISTER_ACK:
        n = 0;
        break;
    case NET_MSG_CREATE_ROOM:
        n = deser_create_room(&msg->create_room, payload, remaining);
        break;
    case NET_MSG_JOIN_ROOM:
        n = deser_join_room(&msg->join_room, payload, remaining);
        break;
    case NET_MSG_ROOM_ASSIGNED:
        n = deser_room_assigned(&msg->room_assigned, payload, remaining);
        break;
    case NET_MSG_QUEUE_MATCHMAKE:
        n = deser_queue_matchmake(&msg->queue_matchmake, payload, remaining);
        break;
    case NET_MSG_QUEUE_STATUS:
        n = deser_queue_status(&msg->queue_status, payload, remaining);
        break;
    case NET_MSG_CHANGE_USERNAME:
        n = deser_change_username(&msg->change_username, payload, remaining);
        break;
    case NET_MSG_STATS_REQUEST:
        n = deser_stats_request(&msg->stats_request, payload, remaining);
        break;
    case NET_MSG_STATS_RESPONSE:
        n = deser_stats_response(&msg->stats_response, payload, remaining);
        break;
    case NET_MSG_LEADERBOARD_REQUEST:
        n = deser_leaderboard_request(&msg->leaderboard_request, payload,
                                      remaining);
        break;
    case NET_MSG_LEADERBOARD_RESPONSE:
        n = deser_leaderboard_response(&msg->leaderboard_response, payload,
                                       remaining);
        break;
    case NET_MSG_SERVER_REGISTER:
        n = deser_server_register(&msg->server_register, payload, remaining);
        break;
    case NET_MSG_SERVER_CREATE_ROOM:
        n = deser_server_create_room(&msg->server_create_room, payload,
                                     remaining);
        break;
    case NET_MSG_SERVER_RESULT:
        n = deser_server_result(&msg->server_result, payload, remaining);
        break;
    case NET_MSG_SERVER_HEARTBEAT:
        n = deser_server_heartbeat(&msg->server_heartbeat, payload, remaining);
        break;
    case NET_MSG_SERVER_ROOM_CREATED:
        n = deser_server_room_created(&msg->server_room_created, payload,
                                      remaining);
        break;
    case NET_MSG_SERVER_ROOM_DESTROYED:
        n = deser_server_room_destroyed(&msg->server_room_destroyed, payload,
                                        remaining);
        break;
    case NET_MSG_SERVER_ELO_RESULT:
        n = deser_server_elo_result(&msg->server_elo_result, payload,
                                    remaining);
        break;
    case NET_MSG_PASS_CONFIRMED:
        n = deser_pass_confirmed(&msg->pass_confirmed, payload, remaining);
        break;
    case NET_MSG_FRIEND_SEARCH:
        n = deser_friend_search(&msg->friend_search, payload, remaining); break;
    case NET_MSG_FRIEND_SEARCH_RESULT:
        n = deser_friend_search_result(&msg->friend_search_result, payload, remaining); break;
    case NET_MSG_FRIEND_REQUEST:
        n = deser_friend_request(&msg->friend_request, payload, remaining); break;
    case NET_MSG_FRIEND_ACCEPT:
        n = deser_friend_accept(&msg->friend_accept, payload, remaining); break;
    case NET_MSG_FRIEND_REJECT:
        n = deser_friend_reject(&msg->friend_reject, payload, remaining); break;
    case NET_MSG_FRIEND_REMOVE:
        n = deser_friend_remove(&msg->friend_remove, payload, remaining); break;
    case NET_MSG_FRIEND_LIST_REQUEST:
        n = deser_friend_list_request(&msg->friend_list_request, payload, remaining); break;
    case NET_MSG_FRIEND_LIST:
        n = deser_friend_list(&msg->friend_list, payload, remaining); break;
    case NET_MSG_FRIEND_UPDATE:
        n = deser_friend_update(&msg->friend_update, payload, remaining); break;
    case NET_MSG_FRIEND_REQUEST_NOTIFY:
        n = deser_friend_request_notify(&msg->friend_request_notify, payload, remaining); break;
    case NET_MSG_ROOM_INVITE:
        n = deser_room_invite(&msg->room_invite, payload, remaining); break;
    case NET_MSG_ROOM_INVITE_NOTIFY:
        n = deser_room_invite_notify(&msg->room_invite_notify, payload, remaining); break;
    case NET_MSG_ROOM_INVITE_EXPIRED:
        n = deser_room_invite_expired(&msg->room_invite_expired, payload, remaining); break;
    default:
        return -1;
    }

    if (n < 0)
        return -1;
    return (int)(off + (size_t)n);
}

int net_msg_write_framed(const NetMsg *msg, uint8_t *buf, size_t buf_size)
{
    /* Serialize into temp area after frame header */
    if (buf_size <= NET_FRAME_HEADER_SIZE)
        return -1;
    uint8_t *payload = buf + NET_FRAME_HEADER_SIZE;
    size_t payload_space = buf_size - NET_FRAME_HEADER_SIZE;
    int payload_len = net_msg_serialize(msg, payload, payload_space);
    if (payload_len < 0)
        return -1;
    /* Write length header */
    uint32_t len32 = (uint32_t)payload_len;
    memcpy(buf, &len32, 4);
    return NET_FRAME_HEADER_SIZE + payload_len;
}

/* ================================================================
 * InputCmd Network Relevance Filter
 * ================================================================ */

/* Indexed by InputCmdType values from core/input_cmd.h.
 * _Static_assert below ensures this stays in sync with the enum. */
static const bool INPUT_RELEVANT[INPUT_CMD_COUNT] = {
    [INPUT_CMD_NONE]                 = false,
    [INPUT_CMD_CONFIRM]              = true,
    [INPUT_CMD_CANCEL]               = false, /* client-only (pause menu) */
    [INPUT_CMD_SELECT_CARD]          = true,
    [INPUT_CMD_PLAY_CARD]            = true,
    [INPUT_CMD_START_GAME]           = true,
    [INPUT_CMD_QUIT]                 = false, /* client-only */
    [INPUT_CMD_CLICK]                = false, /* client-only hit-testing */
    [INPUT_CMD_SELECT_CONTRACT]      = true,
    [INPUT_CMD_SELECT_TRANSMUTATION] = true,
    [INPUT_CMD_APPLY_TRANSMUTATION]  = true,
    [INPUT_CMD_OPEN_SETTINGS]        = false, /* client-only */
    [INPUT_CMD_SETTING_PREV]         = false, /* client-only */
    [INPUT_CMD_SETTING_NEXT]         = false, /* client-only */
    [INPUT_CMD_APPLY_DISPLAY]        = false, /* client-only */
    [INPUT_CMD_ROGUE_PICK]           = false, /* client-only */
    [INPUT_CMD_ROGUE_REVEAL]         = true,
    [INPUT_CMD_DUEL_PICK]            = true,
    [INPUT_CMD_DUEL_GIVE]            = true,
    [INPUT_CMD_DUEL_RETURN]          = true,
    [INPUT_CMD_DEALER_DIR]           = true,
    [INPUT_CMD_DEALER_AMT]           = true,
    [INPUT_CMD_DEALER_CONFIRM]       = true,
    [INPUT_CMD_RETURN_TO_MENU]       = false, /* client-only */
    [INPUT_CMD_LOGIN_SUBMIT]         = false, /* client-only */
    [INPUT_CMD_LOGIN_RETRY]          = false, /* client-only */
    [INPUT_CMD_OPEN_PLAY]            = false, /* client-only */
    [INPUT_CMD_ONLINE_CREATE]        = false, /* client-only */
    [INPUT_CMD_ONLINE_JOIN]          = false, /* client-only */
    [INPUT_CMD_ONLINE_QUICKMATCH]    = false, /* client-only */
    [INPUT_CMD_ONLINE_CANCEL]        = false, /* client-only */
    [INPUT_CMD_OPEN_STATS]           = false, /* client-only */
};
_Static_assert(sizeof(INPUT_RELEVANT) / sizeof(INPUT_RELEVANT[0]) == INPUT_CMD_COUNT,
               "INPUT_RELEVANT must cover every InputCmdType");

bool net_input_cmd_is_relevant(uint8_t cmd_type)
{
    if (cmd_type >= sizeof(INPUT_RELEVANT) / sizeof(INPUT_RELEVANT[0]))
        return false;
    return INPUT_RELEVANT[cmd_type];
}

/* ================================================================
 * Player View Builder (anti-cheat filtering)
 * ================================================================ */

void net_build_player_view(NetPlayerView *out, const struct GameState *gs,
                           const struct Phase2State *p2, uint8_t flow_step,
                           float turn_timer, uint8_t pass_subphase,
                           int dealer_seat, int current_turn_player, int seat)
{
    memset(out, 0, sizeof(*out));

    out->my_seat = (uint8_t)seat;
    out->phase = (uint8_t)gs->phase;
    out->flow_step = flow_step;
    out->pass_subphase = pass_subphase;

    out->round_number = (uint16_t)gs->round_number;
    out->pass_direction = (uint8_t)gs->pass_direction;
    out->pass_card_count = (uint8_t)gs->pass_card_count;
    out->lead_player = (uint8_t)gs->lead_player;
    out->hearts_broken = gs->hearts_broken;
    out->tricks_played = (uint8_t)gs->tricks_played;

    /* Own hand: full card identities */
    const Hand *my_hand = &gs->players[seat].hand;
    out->hand_count = (uint8_t)my_hand->count;
    for (int i = 0; i < my_hand->count; i++)
        out->hand[i] = net_card_from_game(my_hand->cards[i]);

    /* All players' hand counts */
    for (int p = 0; p < NUM_PLAYERS; p++)
        out->hand_counts[p] = (uint8_t)gs->players[p].hand.count;

    /* Scores (public knowledge) */
    for (int p = 0; p < NUM_PLAYERS; p++) {
        out->round_points[p] = (int16_t)gs->players[p].round_points;
        out->total_scores[p] = (int16_t)gs->players[p].total_score;
    }

    /* Current trick (played cards are public) */
    const Trick *t = &gs->current_trick;
    out->current_trick.lead_player = (uint8_t)t->lead_player;
    out->current_trick.lead_suit = (uint8_t)t->lead_suit;
    out->current_trick.num_played = (uint8_t)t->num_played;
    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        out->current_trick.cards[i] = net_card_from_game(t->cards[i]);
        out->current_trick.player_ids[i] = (uint8_t)t->player_ids[i];
    }

    /* Pass state */
    for (int p = 0; p < NUM_PLAYERS; p++)
        out->pass_ready[p] = gs->pass_ready[p];

    /* Own pass selections only */
    for (int i = 0; i < MAX_PASS_CARD_COUNT; i++)
        out->my_pass_selections[i] =
            net_card_from_game(gs->pass_selections[seat][i]);

    out->dealer_seat = (int8_t)dealer_seat;
    out->current_turn_player = (int8_t)current_turn_player;
    out->turn_timer = turn_timer;

    /* Phase 2 */
    out->phase2_enabled = (p2 != NULL && p2->enabled);
    if (!out->phase2_enabled)
        return;

    /* Own contracts: full tracking data */
    const PlayerPhase2 *my_p2 = &p2->players[seat];
    out->my_num_contracts = (uint8_t)my_p2->num_active_contracts;
    for (int i = 0; i < my_p2->num_active_contracts; i++) {
        const ContractInstance *ci = &my_p2->contracts[i];
        NetContractView *cv = &out->my_contracts[i];
        cv->contract_id = (int16_t)ci->contract_id;
        cv->revealed = ci->revealed;
        cv->completed = ci->completed;
        cv->failed = ci->failed;
        cv->tricks_won = (int16_t)ci->tricks_won;
        cv->points_taken = (int16_t)ci->points_taken;
        for (int s = 0; s < SUIT_COUNT; s++)
            cv->cards_collected[s] = (int16_t)ci->cards_collected[s];
        cv->tricks_won_mask = ci->tricks_won_mask;
        cv->max_streak = (int16_t)ci->max_streak;
        cv->paired_transmutation_id = (int16_t)ci->paired_transmutation_id;
    }

    /* Opponent contracts: only revealed info */
    for (int p = 0; p < NUM_PLAYERS; p++) {
        NetOpponentContracts *oc = &out->opponent_contracts[p];
        const PlayerPhase2 *pp = &p2->players[p];
        oc->num_contracts = (uint8_t)pp->num_active_contracts;
        for (int i = 0; i < NET_MAX_CONTRACTS; i++) {
            if (i < pp->num_active_contracts) {
                const ContractInstance *ci = &pp->contracts[i];
                oc->revealed[i] = ci->revealed;
                oc->completed[i] = ci->completed;
                /* Reveal contract_id if own data, revealed, or completed
                 * (completed contracts become public at scoring) */
                oc->contract_ids[i] =
                    (p == seat || ci->revealed || ci->completed)
                        ? (int16_t)ci->contract_id
                        : -1;
            } else {
                oc->revealed[i] = false;
                oc->completed[i] = false;
                oc->contract_ids[i] = -1;
            }
        }
    }

    /* Own transmutation inventory (only valid entries) */
    int inv_cnt = my_p2->transmute_inv.count;
    if (inv_cnt > NET_MAX_TRANSMUTE_INV)
        inv_cnt = NET_MAX_TRANSMUTE_INV;
    out->transmute_inv_count = (uint8_t)inv_cnt;
    for (int i = 0; i < inv_cnt; i++)
        out->transmute_inv[i] = (int16_t)my_p2->transmute_inv.items[i];

    /* Own hand transmute state */
    for (int i = 0; i < my_hand->count; i++) {
        const TransmuteSlot *ts = &my_p2->hand_transmutes.slots[i];
        NetTransmuteSlotView *ns = &out->hand_transmutes[i];
        ns->transmutation_id = (int16_t)ts->transmutation_id;
        ns->original_card = net_card_from_game(ts->original_card);
        ns->transmuter_player = (int8_t)ts->transmuter_player;
        ns->fogged = ts->fogged;
        ns->fog_transmuter = (int8_t)ts->fog_transmuter;
    }

    /* Own draft state */
    const DraftState *ds = &p2->round.draft;
    out->draft_active = ds->active;
    out->draft_current_round = (uint8_t)ds->current_round;
    if (ds->active) {
        const DraftPlayerState *dps = &ds->players[seat];
        out->my_draft.available_count = (uint8_t)dps->available_count;
        for (int i = 0; i < DRAFT_GROUP_SIZE; i++) {
            out->my_draft.available[i].contract_id =
                (int16_t)dps->available[i].contract_id;
            out->my_draft.available[i].transmutation_id =
                (int16_t)dps->available[i].transmutation_id;
        }
        out->my_draft.pick_count = (uint8_t)dps->pick_count;
        for (int i = 0; i < DRAFT_PICKS_PER_PLAYER; i++) {
            out->my_draft.picked[i].contract_id =
                (int16_t)dps->picked[i].contract_id;
            out->my_draft.picked[i].transmutation_id =
                (int16_t)dps->picked[i].transmutation_id;
        }
        out->my_draft.has_picked_this_round = dps->has_picked_this_round;
    }

    /* Trick transmute info (visible to all) — left zeroed here.
     * The server populates trick_transmutes after this call from
     * ServerGame.current_tti (see sv_broadcast_state in server_net.c). */

    /* Persistent effects (all players, visible to all) */
    int eff_count = 0;
    for (int p = 0; p < NUM_PLAYERS; p++) {
        const PlayerPhase2 *pp = &p2->players[p];
        for (int i = 0; i < pp->num_persistent; i++) {
            if (eff_count >= NET_MAX_PLAYERS * NET_MAX_EFFECTS)
                break;
            const ActiveEffect *ae = &pp->persistent_effects[i];
            NetActiveEffectView *nv = &out->persistent_effects[eff_count];
            nv->effect_type = (uint8_t)ae->effect.type;
            /* Flatten the param union — use the largest member */
            nv->param_value = (int16_t)ae->effect.param.points_delta;
            nv->scope = (uint8_t)ae->scope;
            nv->source_player = (int8_t)ae->source_player;
            nv->target_player = (int8_t)ae->target_player;
            nv->rounds_remaining = (int16_t)ae->rounds_remaining;
            nv->active = ae->active;
            eff_count++;
        }
    }
    out->num_persistent_effects = (uint8_t)eff_count;

    /* Game-scoped arrays */
    for (int p = 0; p < NUM_PLAYERS; p++) {
        out->shield_tricks_remaining[p] =
            (int8_t)p2->shield_tricks_remaining[p];
        out->curse_force_hearts[p] = p2->curse_force_hearts[p];
        out->anchor_force_suit[p] = (int8_t)p2->anchor_force_suit[p];
        out->binding_auto_win[p] = (int8_t)p2->binding_auto_win[p];
    }

    /* Previous round points */
    for (int p = 0; p < NUM_PLAYERS; p++)
        out->prev_round_points[p] =
            (int16_t)p2->round.prev_round_points[p];

    /* Mirror history (game-scoped, same for all players) */
    out->last_played_transmute_id =
        (int16_t)p2->last_played_transmute_id;
    out->last_played_resolved_effect =
        (uint8_t)p2->last_played_resolved_effect;
    out->last_played_transmuted_card_suit =
        (int8_t)p2->last_played_transmuted_card.suit;
    out->last_played_transmuted_card_rank =
        (int8_t)p2->last_played_transmuted_card.rank;

    /* trick_winner is populated by the caller (server_net.c) */
    out->trick_winner = -1;

    /* Rogue/Duel pending effect winners */
    out->rogue_pending_winner =
        (int8_t)p2->round.transmute_round.rogue_pending_winner;
    out->duel_pending_winner =
        (int8_t)p2->round.transmute_round.duel_pending_winner;

    /* Rogue: public suit reveal — send to ALL clients */
    if (p2->round.transmute_round.rogue_revealed_count >= 0) {
        out->rogue_chosen_suit =
            (int8_t)p2->round.transmute_round.rogue_chosen_suit;
        out->rogue_chosen_target =
            (int8_t)p2->round.transmute_round.rogue_chosen_target;
        int rc = p2->round.transmute_round.rogue_revealed_count;
        if (rc > NET_MAX_HAND_SIZE) rc = NET_MAX_HAND_SIZE;
        out->rogue_revealed_count = (int8_t)rc;
        for (int i = 0; i < rc; i++)
            out->rogue_revealed_cards[i] = net_card_from_game(
                p2->round.transmute_round.rogue_revealed_cards[i]);
    } else {
        out->rogue_chosen_suit = -1;
        out->rogue_chosen_target = -1;
        out->rogue_revealed_count = -1;
    }

    /* Duel */
    out->duel_chosen_card_idx =
        (int8_t)p2->round.transmute_round.duel_chosen_card_idx;
    out->duel_chosen_target =
        (int8_t)p2->round.transmute_round.duel_chosen_target;

    /* Duel: peek — winner and victim see the card, spectators don't */
    if (p2->round.transmute_round.duel_chosen_card_idx >= 0 &&
        (seat == p2->round.transmute_round.duel_pending_winner ||
         seat == p2->round.transmute_round.duel_chosen_target)) {
        out->duel_revealed_card = net_card_from_game(
            p2->round.transmute_round.duel_revealed_card);
    } else {
        out->duel_revealed_card = (NetCard){.suit = -1, .rank = -1};
    }
    out->duel_was_swap = (int8_t)p2->round.transmute_round.duel_was_swap;

    /* Round-end transmutation effect tracking */
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        out->martyr_flags[i] = (int8_t)p2->round.transmute_round.martyr_flags[i];
        out->gatherer_reduction[i] =
            (int8_t)p2->round.transmute_round.gatherer_reduction[i];
    }
}

/* ================================================================
 * InputCmd ↔ NetInputCmd Conversion
 * ================================================================ */

void net_input_cmd_to_local(const NetInputCmd *net, InputCmd *out)
{
    memset(out, 0, sizeof(*out));
    out->type = (InputCmdType)net->type;
    out->source_player = (int)net->source_player;

    switch (out->type) {
    case INPUT_CMD_SELECT_CARD:
    case INPUT_CMD_PLAY_CARD:
        out->card.card_index = (int)(int8_t)net->card.card_index;
        out->card.card = net_card_to_game(net->card.card);
        break;
    case INPUT_CMD_SELECT_CONTRACT:
        out->contract.pair_index = (int)net->contract.pair_index;
        break;
    case INPUT_CMD_SELECT_TRANSMUTATION:
        out->transmute_select.inv_slot = (int)net->transmute_select.inv_slot;
        break;
    case INPUT_CMD_APPLY_TRANSMUTATION:
        out->transmute_apply.hand_index = (int)net->transmute_apply.hand_index;
        out->transmute_apply.card.suit = (Suit)net->transmute_apply.card_suit;
        out->transmute_apply.card.rank = (Rank)net->transmute_apply.card_rank;
        break;
    case INPUT_CMD_ROGUE_REVEAL:
        out->rogue_reveal.target_player = (int)net->rogue_reveal.target_player;
        out->rogue_reveal.suit = (int)net->rogue_reveal.suit;
        break;
    case INPUT_CMD_DUEL_PICK:
        out->duel_pick.target_player = (int)net->duel_pick.target_player;
        break;
    case INPUT_CMD_DUEL_GIVE:
        out->duel_give.hand_index = (int)net->duel_give.hand_index;
        break;
    case INPUT_CMD_DEALER_DIR:
        out->dealer_dir.direction = (int)net->dealer_dir.direction;
        break;
    case INPUT_CMD_DEALER_AMT:
        out->dealer_amt.amount = (int)net->dealer_amt.amount;
        break;
    default:
        /* No payload for CONFIRM, CANCEL, START_GAME, QUIT, DEALER_CONFIRM,
         * DUEL_RETURN, etc. */
        break;
    }
}

void net_input_cmd_from_local(const InputCmd *cmd, NetInputCmd *out)
{
    memset(out, 0, sizeof(*out));
    out->type = (uint8_t)cmd->type;
    out->source_player = (uint8_t)cmd->source_player;

    switch (cmd->type) {
    case INPUT_CMD_SELECT_CARD:
    case INPUT_CMD_PLAY_CARD:
        out->card.card_index = (uint8_t)cmd->card.card_index;
        out->card.card = net_card_from_game(cmd->card.card);
        break;
    case INPUT_CMD_SELECT_CONTRACT:
        out->contract.pair_index = (int16_t)cmd->contract.pair_index;
        break;
    case INPUT_CMD_SELECT_TRANSMUTATION:
        out->transmute_select.inv_slot = (int8_t)cmd->transmute_select.inv_slot;
        break;
    case INPUT_CMD_APPLY_TRANSMUTATION:
        out->transmute_apply.hand_index = (int8_t)cmd->transmute_apply.hand_index;
        out->transmute_apply.card_suit = (int8_t)cmd->transmute_apply.card.suit;
        out->transmute_apply.card_rank = (int8_t)cmd->transmute_apply.card.rank;
        break;
    case INPUT_CMD_ROGUE_REVEAL:
        out->rogue_reveal.target_player = (int8_t)cmd->rogue_reveal.target_player;
        out->rogue_reveal.suit = (int8_t)cmd->rogue_reveal.suit;
        break;
    case INPUT_CMD_DUEL_PICK:
        out->duel_pick.target_player = (int8_t)cmd->duel_pick.target_player;
        break;
    case INPUT_CMD_DUEL_GIVE:
        out->duel_give.hand_index = (int8_t)cmd->duel_give.hand_index;
        break;
    case INPUT_CMD_DEALER_DIR:
        out->dealer_dir.direction = (int8_t)cmd->dealer_dir.direction;
        break;
    case INPUT_CMD_DEALER_AMT:
        out->dealer_amt.amount = (int8_t)cmd->dealer_amt.amount;
        break;
    default:
        break;
    }
}
