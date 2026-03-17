#ifndef RENDER_H
#define RENDER_H

/* ============================================================
 * @deps-exports: MenuItem enum, RenderState (chat log, info panel,
 *                particle system), CardVisual, UIButton,
 *                render_init(), render_update(), render_draw(),
 *                render_hit_test_card(), render_hit_test_button(),
 *                render_toggle_card_selection(), render_clear_selection(),
 *                render_hit_test_contract(), render_set_contract_options(),
 *                render_chat_log_push(), render_effect_label()
 * @deps-requires: raylib.h (Rectangle, Vector2), particle.h
 *                 (ParticleSystem), core/card.h (NUM_PLAYERS),
 *                 core/game_state.h (GamePhase, PassSubphase),
 *                 anim.h (EaseType), layout.h, phase2/effect.h
 * @deps-used-by: main.c, render.c
 * @deps-last-changed: 2026-03-17 — Added ParticleSystem field to RenderState
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
#define MAX_CARD_VISUALS 64
#define MENU_ITEM_COUNT  6

/* ---- Card Visual ---- */

typedef struct CardVisual {
    Card     card;
    Vector2  position;
    Vector2  target;
    Vector2  start;
    Vector2  origin;          /* rotation pivot relative to card (pixels) */
    float    rotation;
    float    target_rotation;
    float    start_rotation;
    float    scale;
    float    opacity;
    bool     face_up;
    bool     selected;
    bool     hovered;
    bool     animating;
    float    anim_elapsed;
    float    anim_duration;
    float    anim_delay;
    EaseType anim_ease;
    int      z_order;
} CardVisual;

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

/* ---- Render State ---- */

typedef struct RenderState {
    /* Card visual pool */
    CardVisual cards[MAX_CARD_VISUALS];
    int        card_count;

    /* Per-player hand visual indices into cards[] */
    int hand_visuals[NUM_PLAYERS][MAX_HAND_SIZE];
    int hand_visual_counts[NUM_PLAYERS];

    /* Trick visual indices into cards[] */
    int trick_visuals[CARDS_PER_TRICK];
    int trick_visual_count;

    /* Interaction state */
    int hover_card_index;  /* -1 = none */
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

    /* Grudge token display */
    bool player_has_grudge[NUM_PLAYERS];
    int  player_grudge_attacker[NUM_PLAYERS];

    /* Chat log (ring buffer) */
#define CHAT_LOG_MAX 32
#define CHAT_MSG_LEN 64
    char chat_msgs[CHAT_LOG_MAX][CHAT_MSG_LEN];
    int  chat_head;   /* index of oldest message */
    int  chat_count;  /* number of messages stored (0..CHAT_LOG_MAX) */

    /* Info panel: contract (player 0) */
    char info_contract_name[32];
    char info_contract_desc[128];
    bool info_contract_active;

    /* Info panel: host action */
    char info_host_name[32];
    char info_host_desc[128];
    bool info_host_active;

    /* Info panel: obtained bonuses (persistent effects, player 0) */
#define INFO_BONUS_MAX 8
    char info_bonus_text[INFO_BONUS_MAX][48];
    int  info_bonus_count;

    /* Info panel: grudge/revenge (replaces popup) */
    bool     info_grudge_available;
    char     info_grudge_attacker_name[16];
    UIButton info_revenge_btns[4];
    int      info_revenge_ids[4];
    int      info_revenge_count;
    UIButton info_revenge_skip_btn;
    bool     info_grudge_interactive;

    /* Grudge discard UI */
    bool     grudge_discard_ui;
    int      grudge_pending_attacker;  /* new attacker for discard choice */
    UIButton btn_keep_old_grudge;
    UIButton btn_keep_new_grudge;

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

/* Hit-test mouse position against contract option buttons.
 * Returns index (0-3) into contract_options[], or -1 if no hit. */
int render_hit_test_contract(const RenderState *rs, Vector2 mouse_pos);

/* Populate contract option buttons with names, descriptions, and IDs.
 * ids[], names[], descs[] must have at least count elements. */
void render_set_contract_options(RenderState *rs, const int ids[], int count,
                                 const char *names[], const char *descs[]);

/* Push a message into the chat log ring buffer. */
void render_chat_log_push(RenderState *rs, const char *msg);

/* Convert an ActiveEffect to a short human-readable label. */
const char *render_effect_label(const ActiveEffect *ae, char *buf, int buflen);

#endif /* RENDER_H */
