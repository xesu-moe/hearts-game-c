#ifndef LAYOUT_H
#define LAYOUT_H

/* ============================================================
 * @deps-exports: layout_pass_preview_positions(), PlayerPosition, LayoutConfig,
 *                ScoringTableLayout, layout_hand/trick/score/name/confirm/board/pile/pass_staging()
 * @deps-requires: raylib.h (Rectangle, Vector2)
 * @deps-used-by: layout.c, render.c, pass_phase.c, process_input.c, turn_flow.c
 * @deps-last-changed: 2026-03-22 — Pass animation: added layout_pass_preview_positions() for card preview row positioning
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

/* Staging position for a passed card: between trick area and destination
 * player's hand (~60% toward hand from board center). Cards fan slightly
 * apart using card_index. */
Vector2 layout_pass_staging_position(PlayerPosition dest_pos, int card_index,
                                     int card_count, const LayoutConfig *cfg);

/* Position for a player's trick pile (between trick area and hand). */
Vector2 layout_pile_position(PlayerPosition pos, const LayoutConfig *cfg);

/* Pre-computed scoring table geometry. */
typedef struct ScoringTableLayout {
    float table_x;      /* left edge of column 0 */
    float title_y;      /* "Round Complete" title Y */
    float header_y;     /* column header row Y */
    float col_w;        /* width of each column */
    float row_h;        /* height of each player row */
    float line_y;       /* Y of separator line under header */
    int   num_cols;     /* number of columns (3) */
} ScoringTableLayout;

/* Compute scoring table geometry for a given slide offset (0 = fully visible,
 * negative = sliding in from top). */
void layout_scoring_table(const LayoutConfig *cfg, float slide_y,
                          ScoringTableLayout *out);

/* Y position for a player's row in the scoring table. */
float layout_scoring_row_y(int player_index, const ScoringTableLayout *tbl);

/* Target position for a small card in a player's scoring row.
 * Cards are rendered at ~0.5x scale, overlapping horizontally. */
Vector2 layout_scoring_card_position(int player_index, int card_index,
                                     const LayoutConfig *cfg,
                                     const ScoringTableLayout *tbl);

/* Pre-computed contracts table geometry. */
typedef struct ContractsTableLayout {
    float table_x;      /* left edge of column 1 (Player) */
    float col2_x;       /* left edge of column 2 (Contract) */
    float col3_x;       /* left edge of column 3 (Result) */
    float col3_w;       /* width of column 3 */
    float table_w;      /* total table width */
    float title_y;      /* "Contract Results" title Y */
    float header_y;     /* column header row Y */
    float line_y;       /* Y of separator line under header */
    float row_h;        /* height of each player row */
    float first_row_y;  /* Y of first player row */
} ContractsTableLayout;

/* Compute contracts table geometry. */
void layout_contracts_table(const LayoutConfig *cfg, ContractsTableLayout *out);

/* Y position for a player's row in the contracts table. */
float layout_contracts_row_y(int player_index, const ContractsTableLayout *tbl);

/* Compute positions for a preview row of received cards, centered above
 * the human player's hand. Cards are evenly spaced at hand scale. */
void layout_pass_preview_positions(int card_count, const LayoutConfig *cfg,
                                   Vector2 out_positions[]);

/* Recalculate layout dimensions for a new screen size.
 * Scales all dimensions proportionally from 720p reference. */
void layout_recalculate(LayoutConfig *cfg, int screen_width, int screen_height);

#endif /* LAYOUT_H */
