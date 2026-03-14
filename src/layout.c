/* ============================================================
 * @deps-implements: layout.h
 * @deps-requires: layout.h, raylib.h, math.h
 * @deps-last-changed: 2026-03-14 — Fan arc layout for card hands
 * ============================================================ */

#include "layout.h"

#include <math.h>

/* ---- Fan arc constants ---- */

#define FAN_ARC_BOTTOM   45.0f   /* degrees — total angular spread for human player */
#define FAN_ARC_OTHER    30.0f   /* degrees — angular spread for opponent players */
#define FAN_RADIUS_BOTTOM 800.0f /* virtual circle radius for human player */
#define FAN_RADIUS_OTHER  600.0f /* virtual circle radius for opponents */
#define FAN_MAX_CARDS    13.0f   /* card count at which full arc is used */

void layout_hand_positions(PlayerPosition pos, int card_count,
                           const LayoutConfig *cfg,
                           Vector2 out_positions[], float out_rotations[],
                           int *out_count)
{
    *out_count = card_count;
    if (card_count <= 0) return;

    float bx  = (float)cfg->board_x;
    float by  = (float)cfg->board_y;
    float bsz = (float)cfg->board_size;

    float arc_deg, radius, center_angle, rot_sign;
    Vector2 hand_base;

    switch (pos) {
    case POS_BOTTOM:
        arc_deg = FAN_ARC_BOTTOM;
        radius = FAN_RADIUS_BOTTOM;
        center_angle = -PI / 2.0f;
        rot_sign = 1.0f;
        hand_base = (Vector2){bx + bsz * 0.5f, by + bsz - 30.0f};
        break;
    case POS_TOP:
        arc_deg = FAN_ARC_OTHER;
        radius = FAN_RADIUS_OTHER;
        center_angle = PI / 2.0f;
        rot_sign = -1.0f;
        hand_base = (Vector2){bx + bsz * 0.5f, by + 20.0f};
        break;
    case POS_LEFT:
        arc_deg = FAN_ARC_OTHER;
        radius = FAN_RADIUS_OTHER;
        center_angle = 0.0f;
        rot_sign = 1.0f;
        hand_base = (Vector2){bx + 20.0f, by + bsz * 0.5f};
        break;
    case POS_RIGHT:
        arc_deg = FAN_ARC_OTHER;
        radius = FAN_RADIUS_OTHER;
        center_angle = PI;
        rot_sign = -1.0f;
        hand_base = (Vector2){bx + bsz - 20.0f, by + bsz * 0.5f};
        break;
    default:
        *out_count = 0;
        return;
    }

    /* Scale arc spread by card count so small hands don't over-spread */
    float count_ratio = fminf((float)card_count, FAN_MAX_CARDS) / FAN_MAX_CARDS;
    float effective_arc_deg = arc_deg * count_ratio;
    float arc_rad = effective_arc_deg * DEG2RAD;

    /* Arc center is behind the hand base (opposite to center_angle direction) */
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

        out_rotations[i] = rot_sign * t * effective_arc_deg;
    }
}

Vector2 layout_trick_position(PlayerPosition pos, const LayoutConfig *cfg)
{
    float cx = (float)cfg->board_x + (float)cfg->board_size * 0.5f;
    float cy = (float)cfg->board_y + (float)cfg->board_size * 0.5f - 30.0f;
    float offset = 70.0f;

    switch (pos) {
    case POS_BOTTOM: return (Vector2){cx - 40.0f, cy + offset - 20.0f};
    case POS_TOP:    return (Vector2){cx - 40.0f, cy - offset - 60.0f};
    case POS_LEFT:   return (Vector2){cx - offset - 60.0f, cy - 30.0f};
    case POS_RIGHT:  return (Vector2){cx + offset - 20.0f, cy - 30.0f};
    default:         return (Vector2){cx, cy};
    }
}

Vector2 layout_score_position(PlayerPosition pos, const LayoutConfig *cfg)
{
    float bx  = (float)cfg->board_x;
    float by  = (float)cfg->board_y;
    float bsz = (float)cfg->board_size;

    switch (pos) {
    case POS_BOTTOM: return (Vector2){bx + bsz * 0.5f - 60.0f, by + bsz - 20.0f};
    case POS_TOP:    return (Vector2){bx + bsz * 0.5f - 60.0f, by + 5.0f};
    case POS_LEFT:   return (Vector2){bx + 5.0f, by + bsz * 0.5f + 100.0f};
    case POS_RIGHT:  return (Vector2){bx + bsz - 120.0f, by + bsz * 0.5f + 100.0f};
    default:         return (Vector2){0, 0};
    }
}

Vector2 layout_name_position(PlayerPosition pos, const LayoutConfig *cfg)
{
    float bx  = (float)cfg->board_x;
    float by  = (float)cfg->board_y;
    float bsz = (float)cfg->board_size;

    switch (pos) {
    case POS_BOTTOM: return (Vector2){bx + bsz * 0.5f - 30.0f, by + bsz - 35.0f};
    case POS_TOP:    return (Vector2){bx + bsz * 0.5f - 30.0f, by + 5.0f};
    case POS_LEFT:   return (Vector2){bx + 5.0f, by + bsz * 0.5f + 80.0f};
    case POS_RIGHT:  return (Vector2){bx + bsz - 100.0f, by + bsz * 0.5f + 80.0f};
    default:         return (Vector2){0, 0};
    }
}

Vector2 layout_pass_direction_position(const LayoutConfig *cfg)
{
    return (Vector2){
        (float)cfg->board_x + (float)cfg->board_size * 0.5f - 80.0f,
        (float)cfg->board_y + (float)cfg->board_size * 0.5f - 130.0f
    };
}

Rectangle layout_confirm_button(const LayoutConfig *cfg)
{
    float bw = 160.0f;
    float bh = 50.0f;
    return (Rectangle){
        (float)cfg->board_x + ((float)cfg->board_size - bw) * 0.5f,
        (float)cfg->board_y + (float)cfg->board_size * 0.5f + 80.0f,
        bw,
        bh
    };
}

Rectangle layout_board_rect(const LayoutConfig *cfg)
{
    return (Rectangle){
        (float)cfg->board_x, (float)cfg->board_y,
        (float)cfg->board_size, (float)cfg->board_size
    };
}

Vector2 layout_board_center(const LayoutConfig *cfg)
{
    return (Vector2){
        (float)cfg->board_x + (float)cfg->board_size * 0.5f,
        (float)cfg->board_y + (float)cfg->board_size * 0.5f
    };
}
