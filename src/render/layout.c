/* ============================================================
 * @deps-implements: layout.h
 * @deps-requires: layout.h (LayoutConfig, PlayerPosition, ScoringTableLayout),
 *                 card_dimens.h (CARD_WIDTH_REF, CARD_HEIGHT_REF, CARD_OVERLAP_REF),
 *                 raylib.h, math.h
 * @deps-last-changed: 2026-03-20 — Replaced render.h with card_dimens.h to break near-circular dep
 * ============================================================ */

#include "layout.h"
#include "card_dimens.h"

#include <math.h>

/* ---- Fan arc constants (reference values at 720p) ---- */

#define FAN_ARC_BOTTOM   37.0f   /* degrees — total angular spread for human player */
#define FAN_ARC_OTHER    30.0f   /* degrees — angular spread for opponent players */
#define FAN_RADIUS_BOTTOM_REF 950.0f /* virtual circle radius for human player at 720p */
#define FAN_RADIUS_OTHER_REF  600.0f /* virtual circle radius for opponents at 720p */
#define FAN_MAX_CARDS    13.0f   /* card count at which full arc is used */

void layout_recalculate(LayoutConfig *cfg, int screen_width, int screen_height)
{
    cfg->screen_width  = (float)screen_width;
    cfg->screen_height = (float)screen_height;
    cfg->scale         = (float)screen_height / 720.0f;
    cfg->card_width    = CARD_WIDTH_REF  * cfg->scale;
    cfg->card_height   = CARD_HEIGHT_REF * cfg->scale;
    cfg->card_overlap  = CARD_OVERLAP_REF * cfg->scale;
    cfg->board_size    = (float)screen_height;
    /* Shift board to the right so it clears the left panel. */
    float center_x     = ((float)screen_width - cfg->board_size) * 0.5f;
    float panel_margin = center_x;  /* original left margin = panel area */
    cfg->board_x       = center_x + panel_margin * 0.5f;
    cfg->board_y       = 0.0f;
}

void layout_hand_positions(PlayerPosition pos, int card_count,
                           const LayoutConfig *cfg,
                           Vector2 out_positions[], float out_rotations[],
                           int *out_count)
{
    *out_count = card_count;
    if (card_count <= 0) return;

    float s   = cfg->scale;
    float bx  = cfg->board_x;
    float by  = cfg->board_y;
    float bsz = cfg->board_size;

    /* Scale arc spread by card count so small hands don't over-spread */
    float count_ratio = fminf((float)card_count, FAN_MAX_CARDS) / FAN_MAX_CARDS;

    /* ---- Bottom (human) player: independent layout ---- */
    if (pos == POS_BOTTOM) {
        float arc_deg = FAN_ARC_BOTTOM;
        float effective_arc = arc_deg * count_ratio;
        float arc_rad = effective_arc * DEG2RAD;
        float radius = FAN_RADIUS_BOTTOM_REF * s;
        float center_angle = -PI / 2.0f;
        Vector2 hand_base = {bx + bsz * 0.5f, by + bsz - 30.0f * s};
        Vector2 arc_center = {
            hand_base.x - radius * cosf(center_angle),
            hand_base.y - radius * sinf(center_angle)
        };

        for (int i = 0; i < card_count; i++) {
            float t = (card_count == 1)
                          ? 0.0f
                          : ((float)i / (float)(card_count - 1) - 0.5f);
            float card_angle = center_angle + t * arc_rad;
            out_positions[i] = (Vector2){
                arc_center.x + radius * cosf(card_angle),
                arc_center.y + radius * sinf(card_angle)
            };
            out_rotations[i] = t * effective_arc;
        }
        return;
    }

    /* ---- Opponents: compute North, then rotate for East/West ---- */

    float arc_deg = FAN_ARC_OTHER;
    float effective_arc = arc_deg * count_ratio;
    float arc_rad = effective_arc * DEG2RAD;
    float radius = FAN_RADIUS_OTHER_REF * s;

    /* North base: top-center of board */
    float center_angle = PI / 2.0f; /* pointing down from arc center */
    Vector2 north_base = {bx + bsz * 0.5f, by + 20.0f * s};
    Vector2 arc_center = {
        north_base.x - radius * cosf(center_angle),
        north_base.y - radius * sinf(center_angle)
    };

    /* Build North positions left-to-right (negate t in angle calc) */
    Vector2 north_pos[13]; /* FAN_MAX_CARDS */
    float   north_rot[13];

    for (int i = 0; i < card_count; i++) {
        float t = (card_count == 1)
                      ? 0.0f
                      : ((float)i / (float)(card_count - 1) - 0.5f);

        /* Negate t so i=0 lands LEFT of center, i=last lands RIGHT */
        float card_angle = center_angle - t * arc_rad;

        north_pos[i] = (Vector2){
            arc_center.x + radius * cosf(card_angle),
            arc_center.y + radius * sinf(card_angle)
        };
        /* Negate: left card gets CW tilt, right card gets CCW tilt */
        north_rot[i] = -t * effective_arc;
    }

    if (pos == POS_TOP) {
        for (int i = 0; i < card_count; i++) {
            out_positions[i] = north_pos[i];
            out_rotations[i] = north_rot[i];
        }
        return;
    }

    /* Rotate North layout around board center to get East / West.
     * Screen-space CW 90°: (dx,dy) → (-dy, dx)
     * Screen-space CCW 90°: (dx,dy) → (dy, -dx)  */
    float cx = bx + bsz * 0.5f;
    float cy = by + bsz * 0.5f;

    for (int i = 0; i < card_count; i++) {
        float dx = north_pos[i].x - cx;
        float dy = north_pos[i].y - cy;

        if (pos == POS_RIGHT) {
            /* Rotate 90° CW in screen space → right side of board */
            out_positions[i] = (Vector2){cx - dy, cy + dx};
            /* Card faces left (toward center): subtract 90° */
            out_rotations[i] = north_rot[i] - 90.0f;
        } else { /* POS_LEFT */
            /* Rotate 90° CCW in screen space → left side of board */
            out_positions[i] = (Vector2){cx + dy, cy - dx};
            /* Card faces right (toward center): add 90° */
            out_rotations[i] = north_rot[i] + 90.0f;
        }
    }
}

Vector2 layout_trick_position(PlayerPosition pos, const LayoutConfig *cfg)
{
    float s = cfg->scale;
    float cx = cfg->board_x + cfg->board_size * 0.5f;
    float cy = cfg->board_y + cfg->board_size * 0.5f - 30.0f * s;
    float offset = 40.0f * s;

    switch (pos) {
    case POS_BOTTOM: return (Vector2){cx - 40.0f * s, cy + offset - 20.0f * s};
    case POS_TOP:    return (Vector2){cx - 40.0f * s, cy - offset - 60.0f * s};
    case POS_LEFT:   return (Vector2){cx - offset - 60.0f * s, cy - 30.0f * s};
    case POS_RIGHT:  return (Vector2){cx + offset - 20.0f * s, cy - 30.0f * s};
    default:         return (Vector2){cx, cy};
    }
}

Vector2 layout_score_position(PlayerPosition pos, const LayoutConfig *cfg)
{
    float s   = cfg->scale;
    float bx  = cfg->board_x;
    float by  = cfg->board_y;
    float bsz = cfg->board_size;

    switch (pos) {
    case POS_BOTTOM: return (Vector2){bx + bsz * 0.5f - 60.0f * s, by + bsz - 20.0f * s};
    case POS_TOP:    return (Vector2){bx + bsz * 0.5f - 60.0f * s, by + 5.0f * s};
    case POS_LEFT:   return (Vector2){bx + 5.0f * s, by + bsz * 0.5f + 100.0f * s};
    case POS_RIGHT:  return (Vector2){bx + bsz - 120.0f * s, by + bsz * 0.5f + 100.0f * s};
    default:         return (Vector2){0, 0};
    }
}

Vector2 layout_name_position(PlayerPosition pos, const LayoutConfig *cfg)
{
    float s   = cfg->scale;
    float bx  = cfg->board_x;
    float by  = cfg->board_y;
    float bsz = cfg->board_size;

    switch (pos) {
    case POS_BOTTOM: return (Vector2){bx + bsz * 0.5f - 30.0f * s, by + bsz - 35.0f * s};
    case POS_TOP:    return (Vector2){bx + bsz * 0.5f - 30.0f * s, by + 5.0f * s};
    case POS_LEFT:   return (Vector2){bx + 5.0f * s, by + bsz * 0.5f + 80.0f * s};
    case POS_RIGHT:  return (Vector2){bx + bsz - 100.0f * s, by + bsz * 0.5f + 80.0f * s};
    default:         return (Vector2){0, 0};
    }
}

Vector2 layout_pass_direction_position(const LayoutConfig *cfg)
{
    float s = cfg->scale;
    return (Vector2){
        cfg->board_x + cfg->board_size * 0.5f - 80.0f * s,
        cfg->board_y + cfg->board_size * 0.5f - 130.0f * s
    };
}

Rectangle layout_confirm_button(const LayoutConfig *cfg)
{
    float s  = cfg->scale;
    float bw = 160.0f * s;
    float bh = 50.0f * s;
    return (Rectangle){
        cfg->board_x + (cfg->board_size - bw) * 0.5f,
        cfg->board_y + cfg->board_size * 0.5f + 80.0f * s,
        bw,
        bh
    };
}

Rectangle layout_board_rect(const LayoutConfig *cfg)
{
    return (Rectangle){
        cfg->board_x, cfg->board_y,
        cfg->board_size, cfg->board_size
    };
}

Vector2 layout_board_center(const LayoutConfig *cfg)
{
    return (Vector2){
        cfg->board_x + cfg->board_size * 0.5f,
        cfg->board_y + cfg->board_size * 0.5f
    };
}

Rectangle layout_left_panel_upper(const LayoutConfig *cfg)
{
    float pad = 4.0f * cfg->scale;
    /* Panel width = original centered margin (before board shift). */
    float panel_w = (cfg->screen_width - cfg->board_size) * 0.5f;
    float w = panel_w - pad * 2;
    if (w < 0) w = 0;
    return (Rectangle){
        pad, pad, w,
        cfg->screen_height * 0.5f - pad * 2
    };
}

Rectangle layout_left_panel_lower(const LayoutConfig *cfg)
{
    float pad = 4.0f * cfg->scale;
    float half = cfg->screen_height * 0.5f;
    float panel_w = (cfg->screen_width - cfg->board_size) * 0.5f;
    float w = panel_w - pad * 2;
    if (w < 0) w = 0;
    return (Rectangle){
        pad, half + pad, w,
        half - pad * 2
    };
}

Vector2 layout_pass_staging_position(PlayerPosition dest_pos, int card_index,
                                     int card_count, const LayoutConfig *cfg)
{
    float s = cfg->scale;
    float bx = cfg->board_x;
    float by = cfg->board_y;
    float bsz = cfg->board_size;
    float cx = bx + bsz * 0.5f;
    float cy = by + bsz * 0.5f;

    /* Fan spread: cards arranged side-by-side with small gap */
    float fan_gap = 30.0f * s;
    float fan_offset = ((float)card_index - (float)(card_count - 1) * 0.5f) * fan_gap;

    /* Position ~60% from board center toward the destination player's hand edge */
    float extent = bsz * 0.30f;  /* distance from center */

    switch (dest_pos) {
    case POS_BOTTOM:
        return (Vector2){cx + fan_offset, cy + extent - 40.0f * s};
    case POS_TOP:
        return (Vector2){cx + fan_offset, cy - extent};
    case POS_LEFT:
        return (Vector2){cx - extent, cy + fan_offset};
    case POS_RIGHT:
        return (Vector2){cx + extent, cy + fan_offset};
    default:
        return (Vector2){cx, cy};
    }
}

Vector2 layout_pile_position(PlayerPosition pos, const LayoutConfig *cfg)
{
    float s = cfg->scale;
    float cx = cfg->board_x + cfg->board_size * 0.5f;
    float cy = cfg->board_y + cfg->board_size * 0.5f - 30.0f * s;
    float offset = 140.0f * s;

    switch (pos) {
    case POS_BOTTOM: return (Vector2){cx - 10.0f * s, cy + offset + 40.0f * s};
    case POS_TOP:    return (Vector2){cx - 10.0f * s, cy - offset - 40.0f * s};
    case POS_LEFT:   return (Vector2){cx - offset - 120.0f * s, cy};
    case POS_RIGHT:  return (Vector2){cx + offset + 120.0f * s, cy};
    default:         return (Vector2){cx, cy};
    }
}

void layout_scoring_table(const LayoutConfig *cfg, float slide_y,
                          ScoringTableLayout *out)
{
    float s = cfg->scale;
    out->col_w = 150.0f * s;
    out->row_h = 80.0f * s;
    out->num_cols = 4;  /* Player | Round | Total | Cards */
    out->table_x = cfg->board_x +
                   (cfg->board_size - out->col_w * (float)out->num_cols) * 0.5f;
    out->title_y = cfg->board_y + 100.0f * s + slide_y;
    out->header_y = cfg->board_y + 180.0f * s + slide_y;
    out->line_y = out->header_y + 28.0f * s;
}

float layout_scoring_row_y(int player_index, const ScoringTableLayout *tbl)
{
    return tbl->header_y + tbl->row_h * (float)(player_index + 1);
}

Vector2 layout_scoring_card_position(int player_index, int card_index,
                                     const LayoutConfig *cfg,
                                     const ScoringTableLayout *tbl)
{
    float s = cfg->scale;
    float card_area_x = tbl->table_x +
                        tbl->col_w * (float)(tbl->num_cols - 1) - 10.0f * s;
    float row_y = layout_scoring_row_y(player_index, tbl);

    /* Small cards at 0.5x scale, overlapping by ~60% of card width */
    float small_w = CARD_WIDTH_REF * 0.55f * s;
    float overlap = small_w * 0.4f;

    return (Vector2){
        card_area_x + (float)card_index * overlap,
        row_y + 15.0f * s  /* slight vertical offset to center with text */
    };
}

void layout_contracts_table(const LayoutConfig *cfg, ContractsTableLayout *out)
{
    float s = cfg->scale;
    float cx = cfg->board_x + cfg->board_size * 0.5f;

    float col1_w = 140.0f * s;   /* Player */
    float col2_w = 300.0f * s;   /* Contract (name + desc) */
    out->col3_w  = 120.0f * s;   /* Result */
    out->table_w = col1_w + col2_w + out->col3_w;
    out->table_x = cx - out->table_w * 0.5f;
    out->col2_x  = out->table_x + col1_w;
    out->col3_x  = out->col2_x + col2_w;
    out->title_y = cfg->board_y + 100.0f * s;
    out->header_y = cfg->board_y + 180.0f * s;
    out->line_y  = out->header_y + 28.0f * s;
    out->row_h   = 70.0f * s;
    out->first_row_y = out->line_y + 10.0f * s;
}

float layout_contracts_row_y(int player_index, const ContractsTableLayout *tbl)
{
    return tbl->first_row_y + tbl->row_h * (float)player_index;
}

void layout_contract_options(const LayoutConfig *cfg, int count,
                             Rectangle out_rects[])
{
    float s = cfg->scale;
    float btn_w = 260.0f * s;
    float btn_h = 52.0f * s;
    float btn_gap = 6.0f * s;
    float total_h = (float)count * btn_h + (float)(count - 1) * btn_gap;

    /* Get confirm button position to stack above it */
    Rectangle confirm = layout_confirm_button(cfg);
    float bottom_y = confirm.y - 20.0f * s;
    float top_y = bottom_y - total_h;

    float cx = cfg->board_x + cfg->board_size * 0.5f;

    for (int i = 0; i < count; i++) {
        out_rects[i] = (Rectangle){
            cx - btn_w * 0.5f,
            top_y + (float)i * (btn_h + btn_gap),
            btn_w,
            btn_h
        };
    }
}
