#ifndef RENDER_H
#define RENDER_H

/* ============================================================
 * @deps-exports: DragState, RenderState (drag), MenuItem, UIButton,
 *                TOSS_CLICK, TOSS_FLICK, TOSS_DROP, TOSS_CANCEL,
 *                render_init(), render_update(), render_draw(),
 *                render_hit_test_card(), render_hit_test_transmute(),
 *                render_hit_test_button(), render_toggle_card_selection(),
 *                render_clear_selection(), render_hit_test_contract(),
 *                render_set_contract_options(), render_chat_log_push(),
 *                render_chat_log_push_color(), render_effect_label(),
 *                CHAT_LOG_MAX, CHAT_MSG_LEN
 * @deps-requires: raylib.h (Rectangle, Vector2, Color), particle.h,
 *                 core/card.h (NUM_PLAYERS), core/game_state.h (GamePhase, PHASE_DEALING),
 *                 anim.h (CardVisual, MAX_CARD_VISUALS, EaseType), layout.h,
 *                 phase2/effect.h
 * @deps-used-by: render.c, ai.c, play_phase.c, pass_phase.c, turn_flow.c,
 *                process_input.c, update.c, settings_ui.c, info_sync.c,
 *                phase_transitions.c, main.c
 * @deps-last-changed: 2026-03-19 — Moved CardVisual and MAX_CARD_VISUALS to anim.h, added render_cancel_drag
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "anim.h"
#include "core/card.h"
#include "core/game_state.h"
#include "layout.h"
#include "particle.h"
#include "phase2/effect.h"

/* ---- Constants ---- */

/* Reference dimensions at 720p. Runtime sizes come from LayoutConfig. */
#define CARD_WIDTH_REF       80
#define CARD_HEIGHT_REF      120
#define CARD_OVERLAP_REF     30
#define CARD_SELECT_LIFT_REF 20
#define MENU_ITEM_COUNT  6

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

#define SETTINGS_ROW_COUNT     8  /* 5 active + 3 audio placeholders */
#define SETTINGS_ACTIVE_COUNT  5

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
    int selected_indices[PASS_CARD_COUNT]; /* indices into cards[] */
    int selected_count;

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
    PassDirection displayed_pass_dir;
    int           last_trick_winner;
    float         trick_winner_timer;
    float         turn_time_remaining;  /* seconds left for current turn */

    /* UI buttons */
    UIButton menu_items[MENU_ITEM_COUNT];
    UIButton btn_confirm_pass;
    UIButton btn_continue;

    /* Contract selection UI */
    UIButton contract_options[4];
    int      contract_option_count;
    int      contract_option_ids[4];
    int      selected_contract_idx;  /* -1 = none */
    bool     contract_ui_active;

    /* Contract scoring display */
    char     contract_result_text[NUM_PLAYERS][64];
    bool     contract_result_success[NUM_PLAYERS];
    bool     show_contract_results;


    /* Chat log (ring buffer) */
#define CHAT_LOG_MAX 32
#define CHAT_MSG_LEN 192
    char chat_msgs[CHAT_LOG_MAX][CHAT_MSG_LEN];
    int  chat_head;   /* index of oldest message */
    int  chat_count;  /* number of messages stored (0..CHAT_LOG_MAX) */
    Color chat_colors[CHAT_LOG_MAX]; /* per-message color, parallel to chat_msgs */

    /* Info panel: contract (player 0) */
    char info_contract_name[32];
    char info_contract_desc[128];
    bool info_contract_active;

    /* Info panel: vendetta action */
    char info_vendetta_name[32];
    char info_vendetta_desc[128];
    bool info_vendetta_active;

    /* Info panel: obtained bonuses (persistent effects, player 0) */
#define INFO_BONUS_MAX 8
    char info_bonus_text[INFO_BONUS_MAX][48];
    int  info_bonus_count;

    /* Info panel: vendetta options */
#define MAX_VENDETTA_BTNS 4
    bool     vendetta_available;
    UIButton vendetta_btns[MAX_VENDETTA_BTNS];
    int      vendetta_ids[MAX_VENDETTA_BTNS];
    int      vendetta_count;
    UIButton vendetta_skip_btn;
    bool     vendetta_interactive;

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

    /* Settings UI */
    UIButton settings_rows_prev[SETTINGS_ROW_COUNT];
    UIButton settings_rows_next[SETTINGS_ROW_COUNT];
    const char *settings_labels[SETTINGS_ROW_COUNT];
    char        settings_value_bufs[SETTINGS_ROW_COUNT][32];
    bool        settings_disabled[SETTINGS_ROW_COUNT];
    UIButton    btn_settings_back;
    UIButton    btn_settings_apply;  /* "Apply" for display settings */

    /* Mutable layout config */
    LayoutConfig layout;

    /* Layout dirty flag */
    bool layout_dirty;

    /* Flow-driven sync control */
    bool sync_needed;      /* when true, sync_hands() rebuilds visuals */
    int  anim_play_player; /* player whose last card should animate (-1 = none) */

    /* Deal animation */
    bool deal_complete;    /* set by render_update when all deal animations finish */

    /* Particle effects */
    ParticleSystem particles;
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

/* Hit-test a UI button. */
bool render_hit_test_button(const UIButton *btn, Vector2 mouse_pos);

/* Toggle selection of a card visual for passing.
 * Returns new selected_count, or -1 if card is not in human hand. */
int render_toggle_card_selection(RenderState *rs, int card_visual_index);

/* Clear all card selections. */
void render_clear_selection(RenderState *rs);

/* Cancel any active drag state. */
void render_cancel_drag(RenderState *rs);

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

/* Push a message into the chat log ring buffer. */
void render_chat_log_push(RenderState *rs, const char *msg);

/* Push a colored message into the chat log ring buffer. */
void render_chat_log_push_color(RenderState *rs, const char *msg, Color color);

/* Convert an ActiveEffect to a short human-readable label. */
const char *render_effect_label(const ActiveEffect *ae, char *buf, int buflen);

#endif /* RENDER_H */
