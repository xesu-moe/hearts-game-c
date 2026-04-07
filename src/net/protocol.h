#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ============================================================
 * Network protocol: message types, binary serialization, framing,
 * and per-player view building for anti-cheat.
 *
 * This header does NOT include Raylib. It includes only core/card.h
 * and core/input_cmd.h (both Raylib-free) and standard library headers.
 *
 * @deps-exports: NetMsgType (NET_MSG_SERVER_ELO_RESULT = 66),
 *                NetMsgServerEloResult, NetMsgGameOver (has_elo, prev_elo[4], new_elo[4]),
 *                NetMsg (server_elo_result union member), NetCard, NetInputCmd, NetPlayerView
 * @deps-requires: core/card.h (Card, Suit, Rank, NUM_PLAYERS),
 *                 core/input_cmd.h (InputCmd, InputCmdType)
 * @deps-used-by: protocol.c, socket.c, server_net.c, client_net.c, lobby_client.c, lobby_net.c
 * @deps-last-changed: 2026-04-06 — Added NET_MSG_SERVER_ELO_RESULT, NetMsgServerEloResult, and ELO fields to NetMsgGameOver
 * ============================================================ */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/card.h"
#include "core/input_cmd.h"

/* ================================================================
 * Constants
 * ================================================================ */

#define PROTOCOL_VERSION       6
#define NET_MAX_MSG_SIZE       8192 /* 8KB max payload */
#define NET_FRAME_HEADER_SIZE  4    /* uint32_t length prefix */
#define NET_MAX_PLAYERS        4
#define NET_MAX_HAND_SIZE      13
#define NET_MAX_PASS_CARDS     4
#define NET_MAX_CONTRACTS      3
#define NET_MAX_TRANSMUTE_INV  18
#define NET_MAX_EFFECTS        8
#define NET_DRAFT_GROUP_SIZE   4
#define NET_DRAFT_PICKS        3
#define NET_MAX_CHAT_LEN       128
/* Upper bound for serialized NetPlayerView (with phase2).
 * Derived from field sizes — update if NetPlayerView changes.
 *
 * Base: seat(1) + phase(3*1) + round(2+5*1) + hand(1+13*2) + hand_counts(4)
 *       + scores(2*4*2) + trick(4*2+4+3) + pass(4+4*2) + dealer(1+1+4) + p2flag(1)
 *     = 4 + 7 + 27 + 4 + 16 + 15 + 12 + 6 + 1 = 92
 * Phase2: contracts(1 + 3*23) + opp_contracts(4*13) + inv(1 + 8*2) + transmutes(13*7)
 *       + draft(31+2) + trick_transmute(24) + effects(1 + 32*9) + game_scoped(28) = 598
 * Total: 92 + 598 = 690. Use 1024 for headroom. */
#define NET_PLAYER_VIEW_MAX_SIZE 1024
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
    NET_MSG_ROOM_STATUS      = 8,  /* server -> clients: waiting room player list */

    /* Game server messages (20-39) */
    NET_MSG_INPUT_CMD        = 20,
    NET_MSG_STATE_UPDATE     = 21,
    NET_MSG_ROUND_START      = 22,
    NET_MSG_TRICK_RESULT     = 23,
    NET_MSG_GAME_OVER        = 24,
    NET_MSG_PHASE_CHANGE     = 25,
    NET_MSG_REQUEST_ADD_AI   = 26, /* client -> server: add AI to waiting room */
    NET_MSG_REQUEST_REMOVE_AI = 27, /* client -> server: remove last AI */
    NET_MSG_REQUEST_START_GAME = 28, /* client -> server: start game (creator) */
    NET_MSG_PASS_CONFIRMED     = 29, /* server -> clients: a player confirmed pass */

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
    NET_MSG_STATS_REQUEST    = 54,  /* client -> lobby: request full stats */
    NET_MSG_STATS_RESPONSE   = 55,  /* lobby -> client: full stats payload */
    NET_MSG_LEADERBOARD_REQUEST  = 56,  /* client -> lobby: request top 100 */
    NET_MSG_LEADERBOARD_RESPONSE = 57,  /* lobby -> client: top 100 + rank */
    NET_MSG_LOGIN_BY_KEY         = 58,  /* client -> lobby: login with public key */

    /* Friend system messages (70-82) */
    NET_MSG_FRIEND_SEARCH          = 70,
    NET_MSG_FRIEND_SEARCH_RESULT   = 71,
    NET_MSG_FRIEND_REQUEST         = 72,
    NET_MSG_FRIEND_ACCEPT          = 73,
    NET_MSG_FRIEND_REJECT          = 74,
    NET_MSG_FRIEND_REMOVE          = 75,
    NET_MSG_FRIEND_LIST_REQUEST    = 76,
    NET_MSG_FRIEND_LIST            = 77,
    NET_MSG_FRIEND_UPDATE          = 78,
    NET_MSG_FRIEND_REQUEST_NOTIFY  = 79,
    NET_MSG_ROOM_INVITE            = 80,
    NET_MSG_ROOM_INVITE_NOTIFY     = 81,
    NET_MSG_ROOM_INVITE_EXPIRED    = 82,

    /* Lobby <-> Game Server messages (60-69) */
    NET_MSG_SERVER_REGISTER    = 60,
    NET_MSG_SERVER_CREATE_ROOM = 61,
    NET_MSG_SERVER_RESULT      = 62,
    NET_MSG_SERVER_HEARTBEAT   = 63,
    NET_MSG_SERVER_ROOM_CREATED   = 64, /* server -> lobby: room creation ACK */
    NET_MSG_SERVER_ROOM_DESTROYED = 65, /* server -> lobby: room was destroyed */
    NET_MSG_SERVER_ELO_RESULT     = 66, /* lobby -> server: ELO deltas after match */

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
    char     username[NET_MAX_NAME_LEN]; /* display name for waiting room */
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
    uint8_t color_r, color_g, color_b; /* message color (0,0,0 = default LIGHTGRAY) */
    int16_t transmute_id;              /* transmutation tooltip (-1 = none) */
    char    highlight[32];             /* substring to underline (empty = none) */
} NetMsgChat;

typedef struct NetMsgRoomStatus {
    uint8_t player_count;                                  /* 0-4 occupied */
    char    player_names[NET_MAX_PLAYERS][NET_MAX_NAME_LEN]; /* per-seat names */
    uint8_t slot_occupied[NET_MAX_PLAYERS];                /* 1=occupied, 0=empty */
    uint8_t slot_is_ai[NET_MAX_PLAYERS];                   /* 1=AI, 0=human */
} NetMsgRoomStatus;

/* ================================================================
 * Game Message Payloads
 * ================================================================ */

/* Network-safe InputCmd (no Vector2, no client-only commands) */
typedef struct NetInputCmd {
    uint8_t type;          /* InputCmdType (network-relevant subset) */
    uint8_t source_player; /* 0-3 */
    union {
        struct {
            int8_t card_index;  /* transmutation_id hint (-1 = non-transmuted) */
            NetCard card;
        } card; /* SELECT_CARD, PLAY_CARD */
        struct {
            int16_t pair_index;
        } contract; /* SELECT_CONTRACT */
        struct {
            int8_t inv_slot;
        } transmute_select; /* SELECT_TRANSMUTATION */
        struct {
            int8_t hand_index;
            int8_t card_suit;
            int8_t card_rank;
        } transmute_apply; /* APPLY_TRANSMUTATION */
        struct {
            int8_t target_player;
            int8_t suit;
        } rogue_reveal; /* ROGUE_REVEAL */
        struct {
            int8_t target_player;
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
    /* ELO data (populated when lobby responds) */
    bool    has_elo;
    int32_t prev_elo[NET_MAX_PLAYERS];  /* -1 = AI/unranked */
    int32_t new_elo[NET_MAX_PLAYERS];   /* -1 = AI/unranked */
} NetMsgGameOver;

typedef struct NetMsgPhaseChange {
    uint8_t new_phase;
} NetMsgPhaseChange;

/* Game option bounds (shared between client and server) */
#define GAME_OPT_TIMER_COUNT    5
#define GAME_OPT_TIMER_MAX      4   /* max valid index */
#define GAME_OPT_POINT_COUNT    3
#define GAME_OPT_POINT_MAX      2
#define GAME_OPT_MODE_COUNT     3
#define GAME_OPT_MODE_MAX       2

typedef struct NetMsgStartGame {
    uint8_t ai_difficulty;  /* 0=casual, 1=competitive */
    uint8_t timer_option;   /* 0..GAME_OPT_TIMER_MAX */
    uint8_t point_goal_idx; /* 0..GAME_OPT_POINT_MAX */
    uint8_t gamemode;       /* 0..GAME_OPT_MODE_MAX */
} NetMsgStartGame;

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
    int32_t  elo_rating;
    uint32_t games_played;
    uint32_t games_won;
    char     username[NET_MAX_NAME_LEN]; /* returned by server on key-based login */
} NetMsgLoginAck;

typedef struct NetMsgLoginByKey {
    uint8_t public_key[NET_ED25519_PK_LEN];
} NetMsgLoginByKey;

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

typedef struct NetMsgStatsRequest {
    uint8_t auth_token[NET_AUTH_TOKEN_LEN];
} NetMsgStatsRequest;

typedef struct NetMsgStatsResponse {
    int32_t  elo_rating;
    uint32_t games_played;
    uint32_t games_won;
    int32_t  total_score;
    uint32_t moon_shots;
    uint32_t qos_caught;
    uint32_t contracts_fulfilled;
    uint32_t perfect_rounds;
    uint32_t hearts_collected;
    uint32_t tricks_won;
    int32_t  best_score;    /* lowest single-game score (derived) */
    int32_t  worst_score;   /* highest single-game score (derived) */
    uint16_t avg_placement_x100; /* avg placement * 100 (derived) */
} NetMsgStatsResponse;

#define LEADERBOARD_MAX_ENTRIES 100

typedef struct NetLeaderboardEntry {
    char     username[NET_MAX_NAME_LEN];
    int32_t  elo_rating;
    uint32_t games_played;
    uint32_t games_won;
} NetLeaderboardEntry;

typedef struct NetMsgLeaderboardRequest {
    uint8_t auth_token[NET_AUTH_TOKEN_LEN];
} NetMsgLeaderboardRequest;

typedef struct NetMsgLeaderboardResponse {
    uint8_t              entry_count; /* 0-100 */
    NetLeaderboardEntry  entries[LEADERBOARD_MAX_ENTRIES];
    uint16_t             player_rank; /* requesting player's rank (0 = unranked) */
    int32_t              player_elo;
} NetMsgLeaderboardResponse;

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
    /* Per-player stat counters (accumulated over entire game) */
    uint16_t moon_shots[NET_MAX_PLAYERS];
    uint16_t qos_caught[NET_MAX_PLAYERS];
    uint16_t contracts_fulfilled[NET_MAX_PLAYERS];
    uint16_t perfect_rounds[NET_MAX_PLAYERS];
    uint16_t hearts_collected[NET_MAX_PLAYERS];
    uint16_t tricks_won[NET_MAX_PLAYERS];
} NetMsgServerResult;

typedef struct NetMsgServerHeartbeat {
    uint16_t current_rooms;
    uint16_t current_players;
} NetMsgServerHeartbeat;

typedef struct NetMsgServerRoomCreated {
    char    room_code[NET_ROOM_CODE_LEN];
    uint8_t success; /* 1 = created, 0 = failed */
} NetMsgServerRoomCreated;

typedef struct NetMsgServerRoomDestroyed {
    char room_code[NET_ROOM_CODE_LEN];
} NetMsgServerRoomDestroyed;

typedef struct NetMsgServerEloResult {
    char    room_code[NET_ROOM_CODE_LEN];
    int32_t prev_elo[NET_MAX_PLAYERS];  /* -1 = AI/unranked */
    int32_t new_elo[NET_MAX_PLAYERS];   /* -1 = AI/unranked */
} NetMsgServerEloResult;

typedef struct NetMsgPassConfirmed {
    uint8_t seat; /* 0-3: which player just confirmed their pass */
} NetMsgPassConfirmed;

/* ================================================================
 * Friend System Messages
 * ================================================================ */

typedef enum FriendSearchStatus {
    FRIEND_STATUS_AVAILABLE       = 0,
    FRIEND_STATUS_ALREADY_FRIEND  = 1,
    FRIEND_STATUS_PENDING_SENT    = 2,
    FRIEND_STATUS_PENDING_RECEIVED = 3,
    FRIEND_STATUS_BLOCKED         = 4,
    FRIEND_STATUS_SELF            = 5,
} FriendSearchStatus;

typedef enum FriendPresence {
    FRIEND_PRESENCE_OFFLINE  = 0,
    FRIEND_PRESENCE_ONLINE   = 1,
    FRIEND_PRESENCE_IN_GAME  = 2,
} FriendPresence;

typedef struct NetMsgFriendSearch {
    uint8_t auth_token[32];
    char    query[32];
} NetMsgFriendSearch;

typedef struct NetFriendSearchEntry {
    char    username[32];
    int32_t account_id;
    uint8_t status;  /* FriendSearchStatus */
} NetFriendSearchEntry;

typedef struct NetMsgFriendSearchResult {
    uint8_t              count;
    NetFriendSearchEntry results[10];
} NetMsgFriendSearchResult;

typedef struct NetMsgFriendRequest {
    uint8_t auth_token[32];
    int32_t target_account_id;
} NetMsgFriendRequest;

typedef struct NetMsgFriendAccept {
    uint8_t auth_token[32];
    int32_t from_account_id;
} NetMsgFriendAccept;

typedef struct NetMsgFriendReject {
    uint8_t auth_token[32];
    int32_t from_account_id;
} NetMsgFriendReject;

typedef struct NetMsgFriendRemove {
    uint8_t auth_token[32];
    int32_t target_account_id;
} NetMsgFriendRemove;

typedef struct NetMsgFriendListRequest {
    uint8_t auth_token[32];
} NetMsgFriendListRequest;

typedef struct NetFriendEntry {
    char    username[32];
    int32_t account_id;
    uint8_t presence;  /* FriendPresence */
} NetFriendEntry;

typedef struct NetFriendRequestEntry {
    char    username[32];
    int32_t account_id;
} NetFriendRequestEntry;

typedef struct NetMsgFriendList {
    uint8_t             friend_count;
    NetFriendEntry      friends[20];
    uint8_t             request_count;
    NetFriendRequestEntry incoming_requests[10];
} NetMsgFriendList;

typedef struct NetMsgFriendUpdate {
    int32_t account_id;
    uint8_t presence;   /* FriendPresence — 0xFF = removed */
} NetMsgFriendUpdate;

typedef struct NetMsgFriendRequestNotify {
    char    username[32];
    int32_t account_id;
} NetMsgFriendRequestNotify;

typedef struct NetMsgRoomInvite {
    uint8_t auth_token[32];
    int32_t target_account_id;
    char    room_code[8];
} NetMsgRoomInvite;

typedef struct NetMsgRoomInviteNotify {
    char from_username[32];
    char room_code[8];
} NetMsgRoomInviteNotify;

typedef struct NetMsgRoomInviteExpired {
    char room_code[8];
} NetMsgRoomInviteExpired;

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
    int8_t  fog_transmuter[CARDS_PER_TRICK];
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
    float  turn_time_limit;     /* configured turn time (30 + bonus) */

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

    /* Mirror history (game-scoped) */
    int16_t last_played_transmute_id;
    uint8_t last_played_resolved_effect;
    int8_t  last_played_transmuted_card_suit;
    int8_t  last_played_transmuted_card_rank;

    /* Server-authoritative trick winner (for Roulette determinism) */
    int8_t  trick_winner; /* -1 = no complete trick */

    /* Rogue/Duel pending effect winners (-1 = none) */
    int8_t  rogue_pending_winner;
    int8_t  duel_pending_winner;

    /* Rogue: suit reveal (public to all) */
    int8_t  rogue_chosen_suit;       /* -1 = none */
    int8_t  rogue_chosen_target;     /* opponent seat, -1 = none */
    int8_t  rogue_revealed_count;    /* -1 = not resolved, 0 = no match, 1+ = cards */
    NetCard rogue_revealed_cards[NET_MAX_HAND_SIZE]; /* up to 13 */

    /* Duel (-1 = none) */
    int8_t  duel_chosen_card_idx;
    int8_t  duel_chosen_target;
    NetCard duel_revealed_card;   /* actual card for duel peek (private to winner) */
    int8_t  duel_was_swap;        /* -1=pending, 0=returned, 1=swapped */

    /* Round-end transmutation effect tracking */
    int8_t  martyr_flags[NET_MAX_PLAYERS];
    int8_t  gatherer_reduction[NET_MAX_PLAYERS];
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
        NetMsgRoomStatus      room_status;
        NetInputCmd           input_cmd;
        NetPlayerView         state_update;
        NetMsgRoundStart      round_start;
        NetMsgTrickResult     trick_result;
        NetMsgGameOver        game_over;
        NetMsgPhaseChange     phase_change;
        NetMsgRegister        reg;
        NetMsgLogin           login;
        NetMsgLoginAck        login_ack;
        NetMsgLoginByKey      login_by_key;
        NetMsgLoginChallenge  login_challenge;
        NetMsgLoginResponse   login_response;
        NetMsgCreateRoom      create_room;
        NetMsgJoinRoom        join_room;
        NetMsgRoomAssigned    room_assigned;
        NetMsgQueueMatchmake  queue_matchmake;
        NetMsgQueueStatus     queue_status;
        NetMsgChangeUsername  change_username;
        NetMsgStatsRequest    stats_request;
        NetMsgStatsResponse   stats_response;
        NetMsgLeaderboardRequest  leaderboard_request;
        NetMsgLeaderboardResponse leaderboard_response;
        NetMsgServerRegister    server_register;
        NetMsgServerCreateRoom  server_create_room;
        NetMsgServerResult      server_result;
        NetMsgServerHeartbeat   server_heartbeat;
        NetMsgServerRoomCreated   server_room_created;
        NetMsgServerRoomDestroyed server_room_destroyed;
        NetMsgServerEloResult     server_elo_result;
        NetMsgPassConfirmed       pass_confirmed;
        NetMsgStartGame           start_game;
        /* Friend system */
        NetMsgFriendSearch        friend_search;
        NetMsgFriendSearchResult  friend_search_result;
        NetMsgFriendRequest       friend_request;
        NetMsgFriendAccept        friend_accept;
        NetMsgFriendReject        friend_reject;
        NetMsgFriendRemove        friend_remove;
        NetMsgFriendListRequest   friend_list_request;
        NetMsgFriendList          friend_list;
        NetMsgFriendUpdate        friend_update;
        NetMsgFriendRequestNotify friend_request_notify;
        NetMsgRoomInvite          room_invite;
        NetMsgRoomInviteNotify    room_invite_notify;
        NetMsgRoomInviteExpired   room_invite_expired;
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
