#ifndef RENDER_H
#define RENDER_H

/* ============================================================
 * @deps-exports: struct DragState, struct RenderState (elo_has_data,
 *                elo_prev[4], elo_new[4])
 * @deps-requires: raylib.h, anim.h (CardVisual), layout.h, core/game_state.h
 * @deps-used-by: render.c, game/process_input.c, game/update.c, game/info_sync.c,
 *                game/play_phase.c, game/phase_transitions.c, main.c
 * @deps-last-changed: 2026-04-06 — Added elo_has_data (bool), elo_prev[4], elo_new[4] fields for ELO result display
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "anim.h"
#include "core/card.h"
#include "core/game_state.h"
#include "layout.h"
#include "particle.h"
#include "help_menu.h"
#include "phase2/effect.h"

/* Forward declaration — full definition in phase2/phase2_state.h */
struct Phase2State;

#include "game/friend_panel.h"
#include "net/lobby_client.h"

/* Stats screen tabs */
#define STATS_TAB_COUNT 2
typedef enum StatsTab {
    STATS_TAB_GAME_STATS   = 0,
    STATS_TAB_LEADERBOARDS = 1,
} StatsTab;

/* ---- Constants ---- */

#include "card_dimens.h"
#define MENU_ITEM_COUNT  5
#define MAX_PILE_CARDS   52  /* 13 tricks x 4 cards */

/* Trick history record for tooltip display */
#define MAX_TRICKS_PER_ROUND 13
typedef struct TrickRecord {
    Card cards[CARDS_PER_TRICK];
    int  player_ids[CARDS_PER_TRICK];
    int  transmute_ids[CARDS_PER_TRICK]; /* transmutation ID per card, -1=none */
    int  winner;
    int  num_played;
} TrickRecord;

/* ---- Menu Items ---- */

typedef enum MenuItem {
    MENU_PLAY          = 0,
    MENU_STATISTICS    = 1,
    MENU_ACHIEVEMENTS  = 2,
    MENU_SETTINGS      = 3,
    MENU_EXIT          = 4,
} MenuItem;

/* ---- UI Button ---- */

typedef struct UIButton {
    Rectangle   bounds;
    const char *label;
    const char *subtitle;  /* secondary text, e.g. "(Coming Soon)", NULL if none */
    bool        hovered;
    bool        pressed;
    bool        visible;
    bool        disabled;  /* grayed out and non-interactive */
} UIButton;

#define SETTINGS_ROW_COUNT     8  /* 3 display + 2 gameplay + 3 audio */
#define SETTINGS_ACTIVE_COUNT  8
#define SETTINGS_TAB_COUNT     3

typedef enum SettingsTab {
    SETTINGS_TAB_DISPLAY  = 0,
    SETTINGS_TAB_GAMEPLAY = 1,
    SETTINGS_TAB_AUDIO    = 2,
} SettingsTab;

/* ---- Pause state ---- */

typedef enum PauseState {
    PAUSE_INACTIVE = 0,
    PAUSE_MENU,
    PAUSE_CONFIRM_MENU,
    PAUSE_CONFIRM_QUIT,
} PauseState;

#define PAUSE_BTN_COUNT 4

/* ---- Scoring subphase ---- */

typedef enum ScoringSubphase {
    SCORE_SUB_CARDS_FLY,            /* cards flying + menu sliding */
    SCORE_SUB_DISPLAY,              /* waiting for Continue */
    SCORE_SUB_COUNT_UP,             /* score ticking */
    SCORE_SUB_DONE,                 /* waiting for Next Round */
    SCORE_SUB_CONTRACTS,            /* showing contracts panel, waiting for Next Round */
} ScoringSubphase;

/* ---- Pass staging ---- */

#define MAX_PASS_STAGED (NUM_PLAYERS * MAX_PASS_CARD_COUNT)  /* 16 */

typedef struct PassStagedCard {
    int  card_visual_idx;  /* index into cards[] */
    int  from_player;
    int  to_player;
    Card card;
} PassStagedCard;

/* ---- Toss mode classification ---- */
enum { TOSS_CLICK = 0, TOSS_FLICK = 1, TOSS_DROP = 2, TOSS_CANCEL = 3 };

/* ---- Drag State ---- */

typedef struct DragState {
    bool    active;          /* currently dragging a card */
    int     card_visual_idx; /* index into rs->cards[] being dragged, -1 if none */
    Vector2 grab_offset;     /* mouse pos minus card top-left at grab time */
    Vector2 current_pos;     /* smoothed card position during drag (pivot coords) */
    Vector2 release_pos;     /* card position at moment of release */
    bool    has_release_pos; /* set on release, consumed by anim setup */
    int     release_mode;    /* TOSS_CLICK/FLICK/DROP/CANCEL */
    /* Velocity tracking (ring buffer for frame-rate-independent smoothing) */
    #define DRAG_VEL_SAMPLES 6
    Vector2 vel_ring[DRAG_VEL_SAMPLES]; /* recent per-frame velocities in px/s */
    int     vel_ring_idx;               /* next write slot */
    int     vel_ring_count;             /* filled slots (0..DRAG_VEL_SAMPLES) */
    Vector2 prev_pos;        /* previous frame position for velocity calc */
    Vector2 velocity;        /* smoothed cursor velocity in px/s at release */
    /* Snap-back state */
    bool    snap_back;       /* card should animate back to hand */
    Vector2 original_pos;    /* card position before drag started */
    float   original_rot;    /* card rotation before drag started */
    int     original_z;      /* z_order before drag started */
    /* Rearranging state */
    int     hand_slot_origin;   /* hand index where drag started */
    int     hand_slot_current;  /* current target slot (for rearranging) */
    bool    is_play_drag;       /* true = can toss to play (player's turn + playable) */
    int     rearrange_map[MAX_HAND_SIZE]; /* maps display slot -> original hand index (excluding dragged card) */
    int     rearrange_count;    /* entries in rearrange_map */
    bool    is_transmute_drag;  /* true = dragging a transmute card, not a hand card */
    int     transmute_slot_origin;  /* transmute inventory slot where drag started */
    int     transmute_slot_current; /* current target slot for rearranging */
} DragState;

/* ---- Render State ---- */

typedef struct RenderState {
    /* Card visual pool */
    CardVisual cards[MAX_CARD_VISUALS];
    int        card_count;

    /* Per-player hand visual indices into cards[] */
    int hand_visuals[NUM_PLAYERS][MAX_HAND_SIZE];
    int hand_visual_counts[NUM_PLAYERS];

    /* Human hand playability (precomputed, transmute-aware) */
    bool card_playable[MAX_HAND_SIZE];

    /* Trick visual indices into cards[] */
    int trick_visuals[CARDS_PER_TRICK];
    int trick_visual_count;

    /* Interaction state */
    int hover_card_index;  /* -1 = none */
    DragState drag;
    int selected_indices[MAX_PASS_CARD_COUNT]; /* indices into cards[] */
    int selected_count;
    int pass_card_limit;  /* max selectable cards (synced from gs->pass_card_count) */

    /* Phase tracking */
    GamePhase current_phase;
    bool      phase_just_changed;
    float     phase_timer;

    /* Pass subphase display */
    PassSubphase pass_subphase;
    float        pass_subphase_remaining;  /* countdown for UI */
    const char  *pass_status_text;         /* waiting/instruction text, or NULL */
    bool         pass_ready_waiting;      /* true after "Ready" pressed (0-card pass) */

    /* Display caches */
    int           displayed_round_points[NUM_PLAYERS];
    int           displayed_total_scores[NUM_PLAYERS];
    int           shield_remaining[NUM_PLAYERS]; /* Shield tricks remaining per player */
    PassDirection displayed_pass_dir;
    int           last_trick_winner;
    float         trick_winner_timer;
    float         turn_time_remaining;  /* seconds left for current turn */
    float         duel_time_remaining;  /* seconds left for duel pick/give, <0 = inactive */

    /* UI buttons */
    UIButton menu_items[MENU_ITEM_COUNT];
    UIButton btn_confirm_pass;
    UIButton btn_continue;

    /* Contract draft UI */
    UIButton contract_options[4];
    int      contract_option_count;
    int      contract_option_ids[4];     /* contract IDs for hit-testing */
    int      selected_contract_idx;      /* -1 = none */
    bool     contract_ui_active;
    float    contract_anim_t;              /* 0→1 scale+alpha fade-in */
    bool     draft_waiting;               /* true = picked, waiting for others */
    float    draft_wait_border_t;         /* 0→1 wrapping orbit timer */
    float    draft_fadeout_t;             /* 1→0 fade for unselected buttons */
    char     draft_btn_labels[4][128];   /* Contract condition description */
    char     draft_btn_subtitles[4][320]; /* "desc\nReward: tmute - desc" */
    int      draft_transmute_ids[4];     /* paired transmutation ID per button, -1 = none */
    int      draft_round_display;        /* 1-3 for UI */
    int      draft_picks_made;           /* 0-3 */

    /* Contract scoring display (3 contracts per player = 12 max) */
#define MAX_CONTRACT_RESULTS 12
    char     contract_result_text[MAX_CONTRACT_RESULTS][64];   /* player name */
    char     contract_result_name[MAX_CONTRACT_RESULTS][32];   /* transmutation name */
    char     contract_result_desc[MAX_CONTRACT_RESULTS][128];  /* contract condition */
    char     contract_result_tdesc[MAX_CONTRACT_RESULTS][128]; /* transmutation desc */
    bool     contract_result_success[MAX_CONTRACT_RESULTS];
    int      contract_result_count;
    bool     show_contract_results;
    float    contract_scroll_y;  /* scroll offset for rewards panel */


    /* Chat log (ring buffer) */
#define CHAT_LOG_MAX 32
#define CHAT_MSG_LEN 192
    char chat_msgs[CHAT_LOG_MAX][CHAT_MSG_LEN];
    int  chat_head;   /* index of oldest message */
    int  chat_count;  /* number of messages stored (0..CHAT_LOG_MAX) */
    Color chat_colors[CHAT_LOG_MAX]; /* per-message color, parallel to chat_msgs */
    char  chat_highlight[CHAT_LOG_MAX][32];  /* substring to underline (empty=none) */
    int   chat_transmute_id[CHAT_LOG_MAX];   /* transmute id for tooltip (-1=none) */
    int   chat_trick_num[CHAT_LOG_MAX];      /* 1-based trick number for tooltip (-1=none) */

    /* Chat hover tooltip — set by draw, consumed by update */
    int   chat_hover_tid;       /* transmute_id from hovered chat highlight, -1=none */
    Rectangle chat_hover_rect;  /* bounding rect of hovered highlight */
    int       chat_hover_trick_num;   /* 1-based trick num from hovered highlight, -1=none */
    Rectangle chat_hover_trick_rect;  /* bounding rect of hovered trick highlight */

    /* Dealer phase UI */
#define DEALER_DIR_BTN_COUNT 3
#define DEALER_AMT_BTN_COUNT 4
    UIButton dealer_dir_btns[DEALER_DIR_BTN_COUNT];  /* Left, Front, Right */
    UIButton dealer_amt_btns[DEALER_AMT_BTN_COUNT];  /* 0, 2, 3, 4 */
    UIButton dealer_confirm_btn;
    int      dealer_selected_dir;   /* 0=left, 1=across, 2=right; -1=none */
    int      dealer_selected_amt;   /* index into amounts; -1=none */
    bool     dealer_ui_active;

    /* Info panel: contracts (player 0, up to 3) */
    char info_contract_name[3][32];
    char info_contract_desc[3][128];
    int  info_contract_transmute_id[3]; /* paired transmutation ID, -1 if none */
    int  info_contract_count;

    /* Transmutation inventory UI (card visuals in left panel) */
#define MAX_TRANSMUTE_BTNS 18  /* matches MAX_TRANSMUTE_INVENTORY */
    int      transmute_btn_ids[MAX_TRANSMUTE_BTNS]; /* TransmutationDef IDs */
    int      transmute_btn_count;
    int      pending_transmutation_id;  /* -1 = none; mirrors main.c state */
    Rectangle transmute_card_rects[MAX_TRANSMUTE_BTNS]; /* hit-test rects */
    float    transmute_card_y;   /* y position of transmute card row */
    int      transmute_drop_target; /* hand card visual idx hovered during transmute drag, -1 = none */

    /* Contract sprite hit-test rects (for tooltip) */
    Rectangle info_contract_sprite_rects[3];
    int       info_contract_sprite_ids[3]; /* transmutation IDs parallel to sprite_rects */
    int       info_contract_sprite_count;

    /* Per-card transmuted flag for player 0 hand */
    int      hand_transmute_ids[MAX_HAND_SIZE]; /* -1 = not transmuted */

    /* Transmute IDs for cards in the current trick */
    int      trick_transmute_ids[CARDS_PER_TRICK]; /* -1 = not transmuted */
    int      mirror_source_tid[CARDS_PER_TRICK];    /* per-slot: transmute_id Mirror copies, -1 = none */
    bool     mirror_morphed[CARDS_PER_TRICK];      /* per-slot: true after burst + sprite changed */
    float    mirror_morph_timer[CARDS_PER_TRICK];  /* per-slot: countdown before morph (seconds) */

    /* Transmutation hover tooltip */
    struct {
        int     transmute_id;  /* -1 = inactive */
        float   anim_t;        /* 0→1 grow-in, 1→0 shrink-out */
        bool    active;        /* mouse currently over a transmute sprite */
        Vector2 anchor;        /* top-left of the hovered card */
        float   anchor_w;      /* card width */
        float   anchor_h;      /* card height */
    } transmute_tooltip;

    /* Trick card hover tooltip (chat log "trick N" hover) */
    struct {
        int     trick_num;   /* 1-based trick number, -1 = inactive */
        float   anim_t;      /* 0→1 grow-in, 1→0 shrink-out */
        bool    active;      /* mouse currently over a trick highlight */
        Vector2 anchor;      /* top-left of the hovered text */
        float   anchor_w;    /* text width */
        float   anchor_h;    /* text height */
    } trick_tooltip;

    /* Settings UI */
    SettingsTab settings_tab;
    UIButton    settings_tab_btns[SETTINGS_TAB_COUNT];
    UIButton settings_rows_prev[SETTINGS_ROW_COUNT];
    UIButton settings_rows_next[SETTINGS_ROW_COUNT];
    const char *settings_labels[SETTINGS_ROW_COUNT];
    char        settings_value_bufs[SETTINGS_ROW_COUNT][32];
    bool        settings_disabled[SETTINGS_ROW_COUNT];
    UIButton    btn_settings_back;
    UIButton    btn_settings_apply;  /* "Apply" for display settings */

    /* Scoring animation state */
    ScoringSubphase score_subphase;
    float           score_anim_timer;
    float           score_menu_slide_y;     /* current Y offset (starts negative = off screen) */
    bool            score_cards_landed;
    bool            score_menu_arrived;
    int             score_countup_round[NUM_PLAYERS];  /* remaining round pts to add */
    float           score_countup_timer;
    bool            score_tick_pending;     /* flag for main.c to play SFX */
    int             contract_reveal_count;  /* how many rows revealed (0..contract_result_count) */
    float           contract_reveal_timer;  /* countdown to next reveal */
    bool            scoring_screen_done;    /* online: true when scoring UI completes */
    bool            scoring_ready_sent;     /* true after last-screen CONFIRM sent to server */
    float           score_auto_timer;       /* 15s auto-advance countdown (online) */

    int             scoring_cards_per_player[NUM_PLAYERS]; /* card count per row for effect indicators */
    int             scoring_martyr[NUM_PLAYERS];           /* martyr effect count per player (2^count multiplier) */
    int             scoring_gatherer[NUM_PLAYERS];         /* gatherer/pendulum reduction per player */

    /* Trick pile visuals — separate from cards[], survives sync_hands() */
    CardVisual pile_cards[MAX_PILE_CARDS];
    int        pile_card_count;
    bool       pile_anim_in_progress;  /* blocks sync_needed during pile collect */
    bool       trick_anim_in_progress; /* blocks sync during online trick animations */
    int        trick_visible_count;   /* caps trick cards shown by sync_hands (flow controls) */

    /* Trick history for tooltip display (cleared each round) */
    TrickRecord trick_history[MAX_TRICKS_PER_ROUND];
    int         trick_history_count;  /* 0..13 */

    /* Pass animation staging */
    PassStagedCard pass_staged[MAX_PASS_STAGED];
    int            pass_staged_count;
    bool           pass_anim_in_progress;  /* blocks sync_needed during toss/wait */
    float          pass_wait_timer;

    /* Pause overlay */
    PauseState pause_state;

    UIButton   pause_btns[PAUSE_BTN_COUNT];     /* Continue, Settings, Return to Menu, Quit */
    UIButton   pause_confirm_yes;
    UIButton   pause_confirm_no;

    /* Settings return path (so settings can return to pause or menu) */
    GamePhase  settings_return_phase;
    bool       settings_return_paused;

    /* Mutable layout config */
    LayoutConfig layout;

    /* Layout dirty flag */
    bool layout_dirty;

    /* Fog shader */
    Shader  fog_shader;
    int     fog_loc_time;
    int     fog_loc_opacity;
    int     fog_loc_aspect;
    bool    fog_shader_loaded;

    /* Per-card fog mode (parallel arrays) */
    uint8_t hand_fog_mode[MAX_HAND_SIZE];
    uint8_t trick_fog_mode[CARDS_PER_TRICK];

    /* Opponent hover (for Rogue/Duel card picking) */
    bool opponent_hover_active;
    Rectangle opponent_indicator_rects[NUM_PLAYERS]; /* name indicator windows per local seat */
    int       opponent_hover_player;                 /* local seat of hovered opponent, -1 = none */
    float     opponent_border_t;                     /* orbiting border timer 0-1 */

    /* Suit selection UI (Rogue) */
    bool      suit_hover_active;                     /* suit windows visible? */
    Rectangle suit_indicator_rects[SUIT_COUNT];      /* hit-test rects */
    int       suit_hover_idx;                        /* hovered suit, -1 = none */
    float     suit_border_t;                         /* orbiting border timer */
    int       rogue_target_player;                   /* stored target for suit pick */

    /* Staged card indices for Rogue/Duel animations.
     * Set by turn_flow, read by sync to preserve card state across resync. */
    int staged_rogue_cv_count;                       /* number of staged rogue cards */
    int staged_rogue_cv_indices[MAX_HAND_SIZE];      /* indices into cards[] */
    int staged_duel_cv_idx;       /* -1 = none */
    int staged_duel_own_cv_idx;   /* return card during exchange, -1 = none */

    /* Rogue reveal border animation (timer border around revealed cards) */
    bool      rogue_border_active;      /* draw the border? */
    float     rogue_border_progress;    /* 0→1 over reveal duration */

    /* Duel reveal border animation (countdown border around revealed card) */
    bool      duel_border_active;       /* draw the border? */
    float     duel_border_progress;     /* 0→1 over duel choose duration */
    bool      duel_watching;            /* true = non-winner passively watching */

    /* Flow-driven sync control */
    bool sync_needed;      /* when true, sync_hands() rebuilds visuals */
    int  anim_play_player; /* player whose last card should animate (-1 = none) */
    int  anim_trick_slot;  /* trick slot to animate (-1 = use last trick visual) */

    /* ELO display (game-over screen) */
    bool    elo_has_data;               /* true when lobby ELO response received */
    int32_t elo_prev[NUM_PLAYERS];      /* -1 = AI/unranked */
    int32_t elo_new[NUM_PLAYERS];       /* -1 = AI/unranked */

    /* Deal animation */
    bool deal_complete;      /* set by render_update when all deal animations finish */
    bool deal_anim;          /* client-side deal animation in progress */

    /* Particle effects */
    ParticleSystem particles;

    /* Custom fonts (multiple sizes for crisp rendering) */
#define FONT_SIZE_COUNT 4
    Font fonts[FONT_SIZE_COUNT];     /* 16, 32, 48, 96 */
    int  font_base_sizes[FONT_SIZE_COUNT];
    bool font_loaded;

    /* Login UI (Step 19) */
    UIButton btn_login_submit;
    UIButton btn_login_retry;
    const struct LoginUIState *login_ui; /* set by main.c, read by render */

    /* Online menu UI (Step 20) */
#define ONLINE_BTN_MAX 5 /* Reconnect + Create Room + Join Room + Quick Match + Back */
    UIButton online_btns[ONLINE_BTN_MAX];
    int      online_btn_count; /* actual count this frame (4 or 5) */
    UIButton btn_online_join_submit;
    UIButton btn_online_ai_diff_prev;
    UIButton btn_online_ai_diff_next;
    UIButton btn_online_add_ai;
    UIButton btn_online_remove_ai;
    UIButton btn_online_start_game;
    UIButton btn_online_cancel;
    UIButton btn_online_try_again;

    /* Game options arrow selectors (left panel in waiting room) */
    UIButton btn_opt_timer_prev, btn_opt_timer_next;
    UIButton btn_opt_points_prev, btn_opt_points_next;
    UIButton btn_opt_mode_prev, btn_opt_mode_next;
    const struct OnlineUIState *online_ui; /* set by main.c, read by render */
    FriendPanelState *friend_panel; /* set by main.c, mutable for input */

    /* Player display names (usernames or default names) */
    char player_names[NUM_PLAYERS][32];

    /* Stats screen */
    StatsTab stats_tab;
    UIButton stats_tab_btns[STATS_TAB_COUNT];
    UIButton btn_stats_back;
    bool     stats_available;  /* true if lobby_client_info() was valid */

    /* Game Stats tab */
    PlayerFullStats stats_data;
    bool            stats_loaded;
    bool            stats_loading;

    /* Leaderboards tab */
    LeaderboardData leaderboard_data;
    bool            leaderboard_loaded;
    bool            leaderboard_loading;
    float           leaderboard_scroll_y;

    /* Help menu */
    HelpMenuState help_menu;
} RenderState;

/* ---- Public API ---- */

/* Initialize render state. Call once after InitWindow(). */
void render_init(RenderState *rs);

/* Synchronize render state with game state and advance animations.
 * dt should be raw_dt (real time, not game-scaled). */
void render_update(const GameState *gs, RenderState *rs, float dt);

/* Draw everything. Reads GameState immutably for text/phase info. */
void render_draw(const GameState *gs, const RenderState *rs);

/* Font-aware text drawing. Uses the loaded custom font if available,
 * otherwise falls back to Raylib's default font. */
void hh_draw_text(const RenderState *rs, const char *text, int x, int y,
                  int font_size, Color color);
int  hh_measure_text(const RenderState *rs, const char *text, int font_size);

/* Hit-test mouse position against human player's hand cards.
 * Returns index into rs->cards[], or -1 if no hit.
 * Tests in reverse z-order (topmost card first). */
int render_hit_test_card(const RenderState *rs, Vector2 mouse_pos);

/* Hit-test mouse position against opponent (players 1-3) hand cards.
 * Returns card visual index (-1 = miss), sets *out_player to the opponent. */
int render_hit_test_opponent_card(const RenderState *rs, Vector2 mouse_pos,
                                   int *out_player);

/* Hit-test a UI button. */
bool render_hit_test_button(const UIButton *btn, Vector2 mouse_pos);

/* Toggle selection of a card visual for passing.
 * Returns new selected_count, or -1 if card is not in human hand. */
int render_toggle_card_selection(RenderState *rs, int card_visual_index);

/* Clear all card selections. */
void render_clear_selection(RenderState *rs);

/* Cancel any active drag state. */
void render_cancel_drag(RenderState *rs);

/* Start dragging a card from the human hand.
 * cv_idx: index into rs->cards[]. hand_slot: index in hand.
 * is_play_drag: true if this drag can toss-to-play. */
void render_start_card_drag(RenderState *rs, int cv_idx, int hand_slot,
                             Vector2 mouse, bool is_play_drag);

/* Commit the rearranged hand order to game state.
 * Moves the card in gs->players[0].hand and shifts transmute IDs.
 * p2 may be NULL if phase2 is disabled. */
void render_commit_hand_reorder(GameState *gs, RenderState *rs,
                                 struct Phase2State *p2);

/* Recompute snap-back target position for the current target slot. */
void render_update_snap_target(RenderState *rs);

/* Reset all non-dragged, non-animating human hand cards to their layout
 * slot positions.  Use after committing a hand reorder when sync_hands
 * may be blocked (e.g. pass_anim_in_progress). */
void render_snap_all_hand_cards(RenderState *rs);

/* Hit-test mouse position against contract option buttons.
 * Returns index (0-3) into contract_options[], or -1 if no hit. */
int render_hit_test_contract(const RenderState *rs, Vector2 mouse_pos);

/* Populate contract option buttons with names, descriptions, and IDs.
 * ids[], names[], descs[] must have at least count elements. */
void render_set_contract_options(RenderState *rs, const int ids[], int count,
                                 const char *names[], const char *descs[]);

/* Start dragging a transmutation card from the inventory.
 * slot: index in transmute_btn_ids[]. */
void render_start_transmute_drag(RenderState *rs, int slot, Vector2 mouse);

/* Commit the rearranged transmutation inventory order.
 * Reorders both rs->transmute_btn_ids[] and p2->players[0].transmute_inv. */
void render_commit_transmute_reorder(RenderState *rs, struct Phase2State *p2);

/* Hit-test mouse position against transmutation card visuals.
 * Returns button index (0..transmute_btn_count-1), or -1 if no hit. */
int render_hit_test_transmute(const RenderState *rs, Vector2 mouse_pos);

/* Clear trick pile visuals (call at round start). */
void render_clear_piles(RenderState *rs);

/* Allocate a new card visual from the pool. Returns index, or -1 if full. */
int render_alloc_card_visual(RenderState *rs);

/* Push a message into the chat log ring buffer. */
void render_chat_log_clear(RenderState *rs);
void render_chat_log_push(RenderState *rs, const char *msg);

/* Push a colored message into the chat log ring buffer. */
void render_chat_log_push_color(RenderState *rs, const char *msg, Color color);

/* Push a colored message with an underlined highlight and transmutation hover tooltip. */
void render_chat_log_push_rich(RenderState *rs, const char *msg, Color color,
                               const char *highlight, int transmute_id);

/* Push a colored message with an underlined highlight and trick card hover tooltip. */
void render_chat_log_push_trick(RenderState *rs, const char *msg, Color color,
                                const char *highlight, int trick_num);


/* Reset render state for returning to main menu mid-game.
 * Clears card visuals, piles, pass staging, and pause state. */
void render_reset_to_menu(RenderState *rs);

#endif /* RENDER_H */
