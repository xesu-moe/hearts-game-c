#ifndef RENDER_H
#define RENDER_H

/* ============================================================
 * @deps-exports: struct RenderState (draft_btn_labels[][], draft_btn_subtitles[][],
 *                draft_round_display, draft_picks_made, contract_result_*[],
 *                info_contract_*[][], info_contract_count, fog_shader,
 *                fog_loc_time, fog_loc_opacity, hand_fog_mode[], trick_fog_mode[])
 * @deps-requires: raylib.h (Shader), anim.h (CardVisual), layout.h,
 *                 core/card.h, core/game_state.h, phase2/effect.h
 * @deps-used-by: render.c, pass_phase.c, info_sync.c, main.c
 * @deps-last-changed: 2026-03-22 — SETTINGS_ROW_COUNT and SETTINGS_ACTIVE_COUNT increased from 8 to 9
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "anim.h"
#include "core/card.h"
#include "core/game_state.h"
#include "layout.h"
#include "particle.h"
#include "phase2/effect.h"

/* Forward declaration — full definition in phase2/phase2_state.h */
struct Phase2State;

/* ---- Constants ---- */

#include "card_dimens.h"
#define MENU_ITEM_COUNT  6
#define MAX_PILE_CARDS   52  /* 13 tricks x 4 cards */

/* ---- Menu Items ---- */

typedef enum MenuItem {
    MENU_PLAY_ONLINE   = 0,
    MENU_PLAY_OFFLINE  = 1,
    MENU_DECK_BUILDING = 2,
    MENU_STATISTICS    = 3,
    MENU_SETTINGS      = 4,
    MENU_EXIT          = 5,
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

#define SETTINGS_ROW_COUNT     9  /* 3 display + 3 gameplay + 3 audio */
#define SETTINGS_ACTIVE_COUNT  9
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
    /* Velocity tracking */
    Vector2 prev_pos;        /* previous frame position for velocity calc */
    Vector2 velocity;        /* cursor velocity in px/s at release */
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

    /* Display caches */
    int           displayed_round_points[NUM_PLAYERS];
    int           displayed_total_scores[NUM_PLAYERS];
    int           shield_remaining[NUM_PLAYERS]; /* Shield tricks remaining per player */
    PassDirection displayed_pass_dir;
    int           last_trick_winner;
    float         trick_winner_timer;
    float         turn_time_remaining;  /* seconds left for current turn */

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
    int  info_contract_count;

    /* Transmutation inventory UI */
#define MAX_TRANSMUTE_BTNS 8  /* matches MAX_TRANSMUTE_INVENTORY */
    UIButton transmute_btns[MAX_TRANSMUTE_BTNS];
    int      transmute_btn_ids[MAX_TRANSMUTE_BTNS]; /* TransmutationDef IDs */
    char     transmute_btn_labels[MAX_TRANSMUTE_BTNS][32];
    int      transmute_btn_count;
    int      pending_transmutation_id;  /* -1 = none; mirrors main.c state */

    /* Transmutation descriptions for info panel */
#define MAX_TRANSMUTE_INFO 8
    char     transmute_info_text[MAX_TRANSMUTE_INFO][64]; /* "ID: desc" */
    int      transmute_info_count;

    /* Per-card transmuted flag for player 0 hand */
    int      hand_transmute_ids[MAX_HAND_SIZE]; /* -1 = not transmuted */

    /* Transmute IDs for cards in the current trick */
    int      trick_transmute_ids[CARDS_PER_TRICK]; /* -1 = not transmuted */

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

    /* Trick pile visuals — separate from cards[], survives sync_hands() */
    CardVisual pile_cards[MAX_PILE_CARDS];
    int        pile_card_count;
    bool       pile_anim_in_progress;  /* blocks sync_needed during pile collect */

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

    /* Flow-driven sync control */
    bool sync_needed;      /* when true, sync_hands() rebuilds visuals */
    int  anim_play_player; /* player whose last card should animate (-1 = none) */

    /* Deal animation */
    bool deal_complete;    /* set by render_update when all deal animations finish */

    /* Particle effects */
    ParticleSystem particles;

    /* Login UI (Step 19) */
    UIButton btn_login_submit;
    UIButton btn_login_retry;
    const struct LoginUIState *login_ui; /* set by main.c, read by render */

    /* Online menu UI (Step 20) */
#define ONLINE_BTN_COUNT 4 /* Create Room, Join Room, Quick Match, Back */
    UIButton online_btns[ONLINE_BTN_COUNT];
    UIButton btn_online_join_submit;
    UIButton btn_online_cancel;
    const struct OnlineUIState *online_ui; /* set by main.c, read by render */
} RenderState;

/* ---- Public API ---- */

/* Initialize render state. Call once after InitWindow(). */
void render_init(RenderState *rs);

/* Synchronize render state with game state and advance animations.
 * dt should be raw_dt (real time, not game-scaled). */
void render_update(const GameState *gs, RenderState *rs, float dt);

/* Draw everything. Reads GameState immutably for text/phase info. */
void render_draw(const GameState *gs, const RenderState *rs);

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

/* Hit-test mouse position against contract option buttons.
 * Returns index (0-3) into contract_options[], or -1 if no hit. */
int render_hit_test_contract(const RenderState *rs, Vector2 mouse_pos);

/* Populate contract option buttons with names, descriptions, and IDs.
 * ids[], names[], descs[] must have at least count elements. */
void render_set_contract_options(RenderState *rs, const int ids[], int count,
                                 const char *names[], const char *descs[]);

/* Hit-test mouse position against transmutation inventory buttons.
 * Returns button index (0..transmute_btn_count-1), or -1 if no hit. */
int render_hit_test_transmute(const RenderState *rs, Vector2 mouse_pos);

/* Clear trick pile visuals (call at round start). */
void render_clear_piles(RenderState *rs);

/* Allocate a new card visual from the pool. Returns index, or -1 if full. */
int render_alloc_card_visual(RenderState *rs);

/* Push a message into the chat log ring buffer. */
void render_chat_log_push(RenderState *rs, const char *msg);

/* Push a colored message into the chat log ring buffer. */
void render_chat_log_push_color(RenderState *rs, const char *msg, Color color);


/* Reset render state for returning to main menu mid-game.
 * Clears card visuals, piles, pass staging, and pause state. */
void render_reset_to_menu(RenderState *rs);

#endif /* RENDER_H */
