#ifndef RENDER_H
#define RENDER_H

/* ============================================================
 * @deps-exports: MenuItem enum, RenderState, CardVisual, UIButton,
 *                render_init(), render_update(), render_draw(),
 *                render_hit_test_card(), render_hit_test_button(),
 *                render_toggle_card_selection(), render_clear_selection(),
 *                MENU_ITEM_COUNT, CARD_WIDTH, CARD_HEIGHT, CARD_OVERLAP,
 *                CARD_SELECT_LIFT, MAX_CARD_VISUALS
 * @deps-requires: raylib.h, card.h, game_state.h, anim.h, layout.h, card_render.h
 * @deps-used-by: render.c, main.c
 * @deps-last-changed: 2026-03-14 — Added MenuItem enum, disabled field to UIButton, menu_items array to RenderState
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "anim.h"
#include "card.h"
#include "game_state.h"
#include "layout.h"

/* ---- Constants ---- */

#define CARD_WIDTH       80
#define CARD_HEIGHT      120
#define CARD_OVERLAP     30
#define CARD_SELECT_LIFT 20
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

    /* Display caches */
    int           displayed_round_points[NUM_PLAYERS];
    int           displayed_total_scores[NUM_PLAYERS];
    PassDirection displayed_pass_dir;
    int           last_trick_winner;
    float         trick_winner_timer;

    /* UI buttons */
    UIButton menu_items[MENU_ITEM_COUNT];
    UIButton btn_confirm_pass;
    UIButton btn_continue;

    /* Layout dirty flag */
    bool layout_dirty;

    /* Flow-driven sync control */
    bool sync_needed;      /* when true, sync_hands() rebuilds visuals */
    int  anim_play_player; /* player whose last card should animate (-1 = none) */
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

#endif /* RENDER_H */
