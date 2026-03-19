#ifndef LAYOUT_H
#define LAYOUT_H

/* ============================================================
 * @deps-exports: PlayerPosition enum, LayoutConfig struct,
 *                layout_hand_positions(), layout_trick_position(),
 *                layout_score_position(), layout_name_position(),
 *                layout_pass_direction_position(), layout_confirm_button(),
 *                layout_board_rect(), layout_board_center(),
 *                layout_contract_options(),
 *                layout_left_panel_upper(), layout_left_panel_lower(),
 *                layout_recalculate()
 * @deps-requires: raylib.h (Rectangle, Vector2)
 * @deps-used-by: layout.c, render.h, render.c, settings.c, process_input.c
 * @deps-last-changed: 2026-03-19 — Extended used_by: process_input module
 * ============================================================ */

#include "raylib.h"

typedef enum PlayerPosition {
    POS_BOTTOM = 0,
    POS_LEFT   = 1,
    POS_TOP    = 2,
    POS_RIGHT  = 3,
    POS_COUNT  = 4
} PlayerPosition;

typedef struct LayoutConfig {
    float screen_width;
    float screen_height;
    float scale;        /* screen_height / 720.0f — UI scale factor */
    float card_width;   /* CARD_WIDTH_REF * scale */
    float card_height;  /* CARD_HEIGHT_REF * scale */
    float card_overlap; /* CARD_OVERLAP_REF * scale */

    /* Board area: the square region where all game content renders.
     * board_x, board_y = top-left corner in screen coords.
     * board_size = width and height of the square. */
    float board_x;
    float board_y;
    float board_size;
} LayoutConfig;

/* Calculate hand card positions for a player in a fan arc layout.
 * out_positions[] receives the pivot point (rotation center) for each card.
 * out_rotations[] receives the rotation angle in degrees for each card.
 * Both arrays must have at least card_count elements.
 * *out_count is set to card_count on success. */
void layout_hand_positions(PlayerPosition pos, int card_count,
                           const LayoutConfig *cfg,
                           Vector2 out_positions[], float out_rotations[],
                           int *out_count);

/* Position for a card played to the trick center. */
Vector2 layout_trick_position(PlayerPosition pos, const LayoutConfig *cfg);

/* Position for score text display. */
Vector2 layout_score_position(PlayerPosition pos, const LayoutConfig *cfg);

/* Position for player name display. */
Vector2 layout_name_position(PlayerPosition pos, const LayoutConfig *cfg);

/* Position for pass direction indicator text. */
Vector2 layout_pass_direction_position(const LayoutConfig *cfg);

/* Rectangle for the confirm/continue button. */
Rectangle layout_confirm_button(const LayoutConfig *cfg);

/* Return the board area as a Rectangle in screen coordinates. */
Rectangle layout_board_rect(const LayoutConfig *cfg);

/* Return the center point of the board area in screen coordinates. */
Vector2 layout_board_center(const LayoutConfig *cfg);

/* Compute rectangles for contract option buttons (stacked vertically,
 * centered in board area above the confirm button).
 * count must be <= 4. */
void layout_contract_options(const LayoutConfig *cfg, int count,
                             Rectangle out_rects[]);

/* Upper half of left column (chat log area). */
Rectangle layout_left_panel_upper(const LayoutConfig *cfg);

/* Lower half of left column (info panel area). */
Rectangle layout_left_panel_lower(const LayoutConfig *cfg);

/* Recalculate layout dimensions for a new screen size.
 * Scales all dimensions proportionally from 720p reference. */
void layout_recalculate(LayoutConfig *cfg, int screen_width, int screen_height);

#endif /* LAYOUT_H */
