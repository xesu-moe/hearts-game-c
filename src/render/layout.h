#ifndef LAYOUT_H
#define LAYOUT_H

/* ============================================================
 * @deps-exports: PlayerPosition, LayoutConfig, layout_hand_positions(),
 *                layout_trick_position(), layout_score_position(),
 *                layout_name_position(), layout_pass_direction_position(),
 *                layout_confirm_button(), layout_board_rect(),
 *                layout_board_center(), layout_contract_options()
 * @deps-requires: raylib.h
 * @deps-used-by: render.h, render.c
 * @deps-last-changed: 2026-03-15 — Added contract option button layout
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
    int screen_width;
    int screen_height;
    int card_width;
    int card_height;
    int card_overlap;

    /* Board area: the square region where all game content renders.
     * board_x, board_y = top-left corner in screen coords.
     * board_size = width and height of the square. */
    int board_x;
    int board_y;
    int board_size;
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

#endif /* LAYOUT_H */
