#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ============================================================
 * Network protocol: message types, binary serialization, framing,
 * and per-player view building for anti-cheat.
 *
 * This header does NOT include Raylib. It includes only core/card.h
 * and core/input_cmd.h (both Raylib-free) and standard library headers.
 *
 * @deps-exports: NetMsgType (including NET_MSG_LOGIN_CHALLENGE,
 *                NET_MSG_LOGIN_RESPONSE, NET_MSG_REGISTER_ACK),
 *                NetMsg, NetCard, NetInputCmd, NetPlayerView,
 *                NetMsgLoginChallenge, NetMsgLoginResponse,
 *                net_frame_write/read, net_msg_serialize/deserialize,
 *                net_build_player_view, net_input_cmd_is_relevant,
 *                net_input_cmd_to_local, net_input_cmd_from_local
 * @deps-requires: core/card.h (Card, Suit, Rank, NUM_PLAYERS, card_to_index),
 *                 core/input_cmd.h (InputCmd, InputCmdType),
 *                 stdbool.h, stdint.h
 * @deps-used-by: protocol.c, socket.c, server_net.c, client_net.h,
 *                reconnect.h, lobby_net.c
 * @deps-last-changed: 2026-03-24 — Step 15: Added challenge-response auth messages
 * ============================================================ */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/card.h"
#include "core/input_cmd.h"

/* ================================================================
 * Constants
 * ================================================================ */

#define PROTOCOL_VERSION       1
#define NET_MAX_MSG_SIZE       8192 /* 8KB max payload */
#define NET_FRAME_HEADER_SIZE  4    /* uint32_t length prefix */
#define NET_MAX_PLAYERS        4
#define NET_MAX_HAND_SIZE      13
#define NET_MAX_PASS_CARDS     4
#define NET_MAX_CONTRACTS      3
#define NET_MAX_TRANSMUTE_INV  8
#define NET_MAX_EFFECTS        8
#define NET_DRAFT_GROUP_SIZE   4
#define NET_DRAFT_PICKS        3
#define NET_MAX_CHAT_LEN       128
/* Conservative upper bound for serialized NetPlayerView (with phase2) */
#define NET_PLAYER_VIEW_MAX_SIZE 4096
#define NET_MAX_NAME_LEN       32
#define NET_AUTH_TOKEN_LEN     32
#define NET_ROOM_CODE_LEN      8
#define NET_ADDR_LEN           64
#define NET_ED25519_PK_LEN     32
#define NET_ED25519_SIG_LEN    64
#define NET_CHALLENGE_LEN      32

/* ================================================================
 * Message Types (numbered ranges)
 * ================================================================ */

typedef enum NetMsgType {
    /* Shared / Connection (0-19) */
    NET_MSG_HANDSHAKE        = 0,
    NET_MSG_HANDSHAKE_ACK    = 1,
    NET_MSG_HANDSHAKE_REJECT = 2,
    NET_MSG_PING             = 3,
    NET_MSG_PONG             = 4,
    NET_MSG_DISCONNECT       = 5,
    NET_MSG_ERROR            = 6,
    NET_MSG_CHAT             = 7,

    /* Game server messages (20-39) */
    NET_MSG_INPUT_CMD        = 20,
    NET_MSG_STATE_UPDATE     = 21,
    NET_MSG_ROUND_START      = 22,
    NET_MSG_TRICK_RESULT     = 23,
    NET_MSG_GAME_OVER        = 24,
    NET_MSG_PHASE_CHANGE     = 25,

    /* Lobby C<->L messages (40-59) */
    NET_MSG_REGISTER         = 40,
    NET_MSG_LOGIN            = 41,
    NET_MSG_LOGIN_ACK        = 42,
    NET_MSG_LOGOUT           = 43,
    NET_MSG_CREATE_ROOM      = 44,
    NET_MSG_JOIN_ROOM        = 45,
    NET_MSG_ROOM_ASSIGNED    = 46,
    NET_MSG_QUEUE_MATCHMAKE  = 47,
    NET_MSG_QUEUE_CANCEL     = 48,
    NET_MSG_QUEUE_STATUS     = 49,
    NET_MSG_LOGIN_CHALLENGE  = 50,  /* lobby -> client: nonce */
    NET_MSG_LOGIN_RESPONSE   = 51,  /* client -> lobby: signature */
    NET_MSG_REGISTER_ACK     = 52,  /* lobby -> client: success (no payload) */
    NET_MSG_CHANGE_USERNAME  = 53,  /* client -> lobby: rename */

    /* Lobby <-> Game Server messages (60-69) */
    NET_MSG_SERVER_REGISTER    = 60,
    NET_MSG_SERVER_CREATE_ROOM = 61,
    NET_MSG_SERVER_RESULT      = 62,
    NET_MSG_SERVER_HEARTBEAT   = 63,
    NET_MSG_SERVER_ROOM_CREATED = 64, /* server -> lobby: room creation ACK */

    NET_MSG_TYPE_COUNT
} NetMsgType;

/* Rejection reasons */
typedef enum NetRejectReason {
    NET_REJECT_VERSION_MISMATCH = 0,
    NET_REJECT_ROOM_FULL        = 1,
    NET_REJECT_BANNED           = 2,
    NET_REJECT_INVALID_TOKEN    = 3,
    NET_REJECT_INVALID_ROOM     = 4
} NetRejectReason;

/* Disconnect reasons */
typedef enum NetDisconnectReason {
    NET_DISCONNECT_NORMAL  = 0,
    NET_DISCONNECT_TIMEOUT = 1,
    NET_DISCONNECT_KICKED  = 2
} NetDisconnectReason;

/* ================================================================
 * Wire-Format Primitive: NetCard
 * ================================================================ */

typedef struct NetCard {
    int8_t suit; /* -1 = CARD_NONE */
    int8_t rank; /* -1 = CARD_NONE */
} NetCard;

/* Convert between game Card and wire NetCard */
static inline NetCard net_card_from_game(Card c)
{
    return (NetCard){.suit = (int8_t)c.suit, .rank = (int8_t)c.rank};
}

static inline Card net_card_to_game(NetCard c)
{
    return (Card){.suit = (Suit)c.suit, .rank = (Rank)c.rank};
}

/* ================================================================
 * Connection Message Payloads
 * ================================================================ */

typedef struct NetMsgHandshake {
    uint16_t protocol_version;
    uint8_t  auth_token[NET_AUTH_TOKEN_LEN];
    char     room_code[NET_ROOM_CODE_LEN];
} NetMsgHandshake;

typedef struct NetMsgHandshakeAck {
    uint16_t protocol_version;
    uint8_t  assigned_seat;                     /* 0-3 */
    uint8_t  session_token[NET_AUTH_TOKEN_LEN]; /* client stores for reconnect */
} NetMsgHandshakeAck;

typedef struct NetMsgHandshakeReject {
    uint16_t server_version;
    uint8_t  reason; /* NetRejectReason */
} NetMsgHandshakeReject;

typedef struct NetMsgPing {
    uint32_t sequence;
    uint32_t timestamp_ms;
} NetMsgPing;

typedef struct NetMsgPong {
    uint32_t sequence;
    uint32_t echo_timestamp_ms;
    uint32_t server_timestamp_ms;
} NetMsgPong;

typedef struct NetMsgDisconnect {
    uint8_t reason; /* NetDisconnectReason */
} NetMsgDisconnect;

typedef struct NetMsgError {
    uint8_t code;
    char    message[NET_MAX_CHAT_LEN];
} NetMsgError;

typedef struct NetMsgChat {
    uint8_t seat;
    char    text[NET_MAX_CHAT_LEN];
} NetMsgChat;

/* ================================================================
 * Game Message Payloads
 * ================================================================ */

/* Network-safe InputCmd (no Vector2, no client-only commands) */
typedef struct NetInputCmd {
    uint8_t type;          /* InputCmdType (network-relevant subset) */
    uint8_t source_player; /* 0-3 */
    union {
        struct {
            uint8_t card_index;
            NetCard card;
        } card; /* SELECT_CARD, PLAY_CARD */
        struct {
            int16_t contract_id;
        } contract; /* SELECT_CONTRACT */
        struct {
            int8_t inv_slot;
        } transmute_select; /* SELECT_TRANSMUTATION */
        struct {
            int8_t hand_index;
        } transmute_apply; /* APPLY_TRANSMUTATION */
        struct {
            int8_t target_player;
            int8_t hand_index;
        } rogue_reveal; /* ROGUE_REVEAL */
        struct {
            int8_t target_player;
            int8_t hand_index;
        } duel_pick; /* DUEL_PICK */
        struct {
            int8_t hand_index;
        } duel_give; /* DUEL_GIVE */
        struct {
            int8_t direction;
        } dealer_dir; /* DEALER_DIR */
        struct {
            int8_t amount;
        } dealer_amt; /* DEALER_AMT */
    };
} NetInputCmd;

typedef struct NetMsgRoundStart {
    uint16_t round_number;
    uint8_t  pass_direction;
    uint8_t  pass_count;
    int8_t   dealer_seat; /* -1 if no dealer */
} NetMsgRoundStart;

typedef struct NetMsgTrickResult {
    uint8_t winner_seat;
    uint8_t points;
    uint8_t trick_num;
} NetMsgTrickResult;

typedef struct NetMsgGameOver {
    int16_t final_scores[NET_MAX_PLAYERS];
    uint8_t winners[NET_MAX_PLAYERS];
    uint8_t winner_count;
} NetMsgGameOver;

typedef struct NetMsgPhaseChange {
    uint8_t new_phase;
} NetMsgPhaseChange;

/* ================================================================
 * Lobby Message Payloads
 * ================================================================ */

typedef struct NetMsgRegister {
    char    username[NET_MAX_NAME_LEN];
    uint8_t public_key[NET_ED25519_PK_LEN];
} NetMsgRegister;

typedef struct NetMsgLogin {
    char username[NET_MAX_NAME_LEN];
} NetMsgLogin;

typedef struct NetMsgLoginChallenge {
    uint8_t nonce[NET_CHALLENGE_LEN];
} NetMsgLoginChallenge;

typedef struct NetMsgLoginResponse {
    uint8_t signature[NET_ED25519_SIG_LEN];
} NetMsgLoginResponse;

typedef struct NetMsgLoginAck {
    uint8_t  auth_token[NET_AUTH_TOKEN_LEN];
    uint16_t elo_rating;
    uint32_t games_played;
    uint32_t games_won;
} NetMsgLoginAck;

typedef struct NetMsgCreateRoom {
    uint8_t auth_token[NET_AUTH_TOKEN_LEN];
} NetMsgCreateRoom;

typedef struct NetMsgJoinRoom {
    uint8_t auth_token[NET_AUTH_TOKEN_LEN];
    char    room_code[NET_ROOM_CODE_LEN];
} NetMsgJoinRoom;

typedef struct NetMsgRoomAssigned {
    char    server_addr[NET_ADDR_LEN];
    uint16_t server_port;
    char    room_code[NET_ROOM_CODE_LEN];
    uint8_t auth_token[NET_AUTH_TOKEN_LEN];
} NetMsgRoomAssigned;

typedef struct NetMsgQueueMatchmake {
    uint8_t auth_token[NET_AUTH_TOKEN_LEN];
} NetMsgQueueMatchmake;

typedef struct NetMsgQueueStatus {
    uint16_t position;
    uint16_t estimated_wait_secs;
} NetMsgQueueStatus;

typedef struct NetMsgChangeUsername {
    uint8_t auth_token[NET_AUTH_TOKEN_LEN];
    char    new_username[NET_MAX_NAME_LEN];
} NetMsgChangeUsername;

/* ================================================================
 * Lobby <-> Game Server Message Payloads
 * ================================================================ */

typedef struct NetMsgServerRegister {
    char     addr[NET_ADDR_LEN];
    uint16_t port;
    uint16_t max_rooms;
    uint16_t current_rooms;
} NetMsgServerRegister;

typedef struct NetMsgServerCreateRoom {
    char    room_code[NET_ROOM_CODE_LEN];
    uint8_t player_tokens[NET_MAX_PLAYERS][NET_AUTH_TOKEN_LEN];
} NetMsgServerCreateRoom;

typedef struct NetMsgServerResult {
    char    room_code[NET_ROOM_CODE_LEN];
    int16_t final_scores[NET_MAX_PLAYERS];
    uint8_t winner_seats[NET_MAX_PLAYERS];
    uint8_t winner_count;
    uint16_t rounds_played;
    uint8_t player_tokens[NET_MAX_PLAYERS][NET_AUTH_TOKEN_LEN];
} NetMsgServerResult;

typedef struct NetMsgServerHeartbeat {
    uint16_t current_rooms;
    uint16_t current_players;
} NetMsgServerHeartbeat;

typedef struct NetMsgServerRoomCreated {
    char    room_code[NET_ROOM_CODE_LEN];
    uint8_t success; /* 1 = created, 0 = failed */
} NetMsgServerRoomCreated;

/* ================================================================
 * NetPlayerView Sub-Structs
 * ================================================================ */

typedef struct NetTrickView {
    NetCard cards[CARDS_PER_TRICK];
    uint8_t player_ids[CARDS_PER_TRICK];
    uint8_t lead_player;
    uint8_t lead_suit;
    uint8_t num_played;
} NetTrickView;

typedef struct NetTransmuteSlotView {
    int16_t transmutation_id; /* -1 = not transmuted */
    NetCard original_card;
    int8_t  transmuter_player;
    bool    fogged;
    int8_t  fog_transmuter;
} NetTransmuteSlotView;

typedef struct NetContractView {
    int16_t  contract_id; /* -1 = none */
    bool     revealed;
    bool     completed;
    bool     failed;
    int16_t  tricks_won;
    int16_t  points_taken;
    int16_t  cards_collected[SUIT_COUNT];
    uint16_t tricks_won_mask;
    int16_t  max_streak;
    int16_t  paired_transmutation_id;
} NetContractView;

typedef struct NetDraftPair {
    int16_t contract_id;
    int16_t transmutation_id;
} NetDraftPair;

typedef struct NetDraftPlayerView {
    NetDraftPair available[NET_DRAFT_GROUP_SIZE];
    uint8_t      available_count;
    NetDraftPair picked[NET_DRAFT_PICKS];
    uint8_t      pick_count;
    bool         has_picked_this_round;
} NetDraftPlayerView;

typedef struct NetActiveEffectView {
    uint8_t effect_type;
    int16_t param_value;
    uint8_t scope;
    int8_t  source_player;
    int8_t  target_player;
    int16_t rounds_remaining;
    bool    active;
} NetActiveEffectView;

typedef struct NetTrickTransmuteView {
    int16_t transmutation_ids[CARDS_PER_TRICK];
    int8_t  transmuter_player[CARDS_PER_TRICK];
    uint8_t resolved_effects[CARDS_PER_TRICK];
    bool    fogged[CARDS_PER_TRICK];
} NetTrickTransmuteView;

/* Opponent contract info (only revealed data) */
typedef struct NetOpponentContracts {
    uint8_t num_contracts;
    bool    revealed[NET_MAX_CONTRACTS];
    bool    completed[NET_MAX_CONTRACTS];
    int16_t contract_ids[NET_MAX_CONTRACTS]; /* -1 if not revealed */
} NetOpponentContracts;

/* ================================================================
 * NetPlayerView (the big per-player state snapshot)
 * ================================================================ */

typedef struct NetPlayerView {
    /* Identity */
    uint8_t my_seat;

    /* Phase / flow */
    uint8_t phase;         /* GamePhase */
    uint8_t flow_step;     /* FlowStep */
    uint8_t pass_subphase; /* PassSubphase */

    /* Round info */
    uint16_t round_number;
    uint8_t  pass_direction;
    uint8_t  pass_card_count;
    uint8_t  lead_player;
    bool     hearts_broken;
    uint8_t  tricks_played;

    /* Own hand (full card identities) */
    NetCard hand[NET_MAX_HAND_SIZE];
    uint8_t hand_count;

    /* All players' hand counts (indexed by seat) */
    uint8_t hand_counts[NET_MAX_PLAYERS];

    /* Scores */
    int16_t round_points[NET_MAX_PLAYERS];
    int16_t total_scores[NET_MAX_PLAYERS];

    /* Current trick */
    NetTrickView current_trick;

    /* Pass state */
    bool    pass_ready[NET_MAX_PLAYERS];
    NetCard my_pass_selections[NET_MAX_PASS_CARDS];

    /* Dealer / Turn */
    int8_t dealer_seat;          /* -1 if no dealer */
    int8_t current_turn_player;  /* -1 if N/A */
    float  turn_timer;           /* seconds remaining */

    /* --- Phase 2 --- */
    bool phase2_enabled;

    /* Own contracts (full tracking) */
    NetContractView my_contracts[NET_MAX_CONTRACTS];
    uint8_t         my_num_contracts;

    /* Opponents' contracts (revealed info only) */
    NetOpponentContracts opponent_contracts[NET_MAX_PLAYERS];

    /* Own transmutation inventory */
    int16_t transmute_inv[NET_MAX_TRANSMUTE_INV];
    uint8_t transmute_inv_count;

    /* Own hand transmute state */
    NetTransmuteSlotView hand_transmutes[NET_MAX_HAND_SIZE];

    /* Own draft state */
    NetDraftPlayerView my_draft;
    uint8_t            draft_current_round;
    bool               draft_active;

    /* Trick transmute info (visible to all) */
    NetTrickTransmuteView trick_transmutes;

    /* Active persistent effects (all players, visible to all) */
    NetActiveEffectView persistent_effects[NET_MAX_PLAYERS * NET_MAX_EFFECTS];
    uint8_t             num_persistent_effects;

    /* Game-scoped state (visible to all) */
    int8_t shield_tricks_remaining[NET_MAX_PLAYERS];
    bool   curse_force_hearts[NET_MAX_PLAYERS];
    int8_t anchor_force_suit[NET_MAX_PLAYERS];
    int8_t binding_auto_win[NET_MAX_PLAYERS];

    /* Previous round points (for dealer determination UI) */
    int16_t prev_round_points[NET_MAX_PLAYERS];
} NetPlayerView;

/* ================================================================
 * NetMsg Tagged Union
 * ================================================================ */

typedef struct NetMsg {
    NetMsgType type;
    union {
        NetMsgHandshake       handshake;
        NetMsgHandshakeAck    handshake_ack;
        NetMsgHandshakeReject handshake_reject;
        NetMsgPing            ping;
        NetMsgPong            pong;
        NetMsgDisconnect      disconnect;
        NetMsgError           error;
        NetMsgChat            chat;
        NetInputCmd           input_cmd;
        NetPlayerView         state_update;
        NetMsgRoundStart      round_start;
        NetMsgTrickResult     trick_result;
        NetMsgGameOver        game_over;
        NetMsgPhaseChange     phase_change;
        NetMsgRegister        reg;
        NetMsgLogin           login;
        NetMsgLoginAck        login_ack;
        NetMsgLoginChallenge  login_challenge;
        NetMsgLoginResponse   login_response;
        NetMsgCreateRoom      create_room;
        NetMsgJoinRoom        join_room;
        NetMsgRoomAssigned    room_assigned;
        NetMsgQueueMatchmake  queue_matchmake;
        NetMsgQueueStatus     queue_status;
        NetMsgChangeUsername  change_username;
        NetMsgServerRegister    server_register;
        NetMsgServerCreateRoom  server_create_room;
        NetMsgServerResult      server_result;
        NetMsgServerHeartbeat   server_heartbeat;
        NetMsgServerRoomCreated server_room_created;
    };
} NetMsg;

/* ================================================================
 * Framing
 * ================================================================ */

/* Write a length-prefixed frame: [4B payload_len LE][payload].
 * Returns total bytes written (4 + payload_len), or -1 on error. */
int net_frame_write(uint8_t *buf, size_t buf_size,
                    const uint8_t *payload, size_t payload_len);

/* Read a complete frame from a stream buffer.
 * Returns bytes consumed (header + payload) on success,
 *         0 if buffer doesn't contain a complete frame yet,
 *        -1 on error (oversized payload). */
int net_frame_read(const uint8_t *buf, size_t buf_len,
                   const uint8_t **out_payload, size_t *out_payload_len);

/* ================================================================
 * Serialization
 * ================================================================ */

/* Serialize a NetMsg to a byte buffer.
 * Returns bytes written, or -1 on error (buffer too small / unknown type). */
int net_msg_serialize(const NetMsg *msg, uint8_t *buf, size_t buf_size);

/* Deserialize a NetMsg from a byte buffer.
 * Returns bytes consumed, or -1 on error (malformed / unknown type). */
int net_msg_deserialize(NetMsg *msg, const uint8_t *buf, size_t buf_len);

/* Convenience: serialize + frame in one call.
 * Returns total framed bytes, or -1 on error. */
int net_msg_write_framed(const NetMsg *msg, uint8_t *buf, size_t buf_size);

/* ================================================================
 * InputCmd Filter
 * ================================================================ */

/* Returns true if this InputCmdType should be sent over the network.
 * Client-only commands (CLICK, SETTINGS, etc.) return false. */
bool net_input_cmd_is_relevant(uint8_t cmd_type);

/* Convert between NetInputCmd (wire format) and InputCmd (game format). */
void net_input_cmd_to_local(const NetInputCmd *net, InputCmd *out);
void net_input_cmd_from_local(const InputCmd *cmd, NetInputCmd *out);

/* ================================================================
 * Player View Builder
 * ================================================================ */

/* Forward declarations — avoids pulling game headers into protocol.h */
struct GameState;
struct Phase2State;

/* Build a per-player view of the game state with hidden information
 * filtered out. The server calls this once per player per state change.
 *
 * out:                 filled with the filtered view
 * gs:                  authoritative GameState
 * p2:                  Phase2State (NULL if phase2 disabled)
 * flow_step:           current FlowStep enum value
 * turn_timer:          seconds remaining for current turn
 * pass_subphase:       current PassSubphase enum value
 * dealer_seat:         dealer player id, -1 if none
 * current_turn_player: whose turn it is, -1 if N/A
 * seat:                which player (0-3) this view is for */
void net_build_player_view(NetPlayerView *out,
                           const struct GameState *gs,
                           const struct Phase2State *p2,
                           uint8_t flow_step,
                           float turn_timer,
                           uint8_t pass_subphase,
                           int dealer_seat,
                           int current_turn_player,
                           int seat);

#endif /* PROTOCOL_H */
