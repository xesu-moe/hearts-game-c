#ifndef ANIM_H
#define ANIM_H

/* ============================================================
 * @deps-exports: CardVisual, MAX_CARD_VISUALS, anim_start(), anim_update(),
 *                anim_toss_enabled(), anim_setup_toss(), anim_set_speed(),
 *                anim_get_speed(), ANIM_PLAY_CARD_DURATION,
 *                ANIM_PASS_CARD_DURATION, ANIM_TRICK_COLLECT_DUR,
 *                ANIM_DEAL_CARD_DURATION, ANIM_DEAL_CARD_STAGGER,
 *                ANIM_TOSS_DURATION, ANIM_SNAP_BACK_DURATION,
 *                ANIM_PASS_TOSS_DURATION, ANIM_PASS_WAIT_DURATION,
 *                ANIM_PASS_RECEIVE_DURATION, PASS_TOSS_STAGGER,
 *                PASS_PLAYER_STAGGER, TOSS_VEL_EXTEND, TOSS_SPIN_FACTOR,
 *                TOSS_SIDEWAYS_FACTOR, TOSS_MAX_SPIN, HOVER_SCALE_TARGET,
 *                HOVER_LIFT_REF, HOVER_ANIM_SPEED
 * @deps-requires: easing.h (EaseType), raylib.h (Vector2), core/card.h (Card)
 * @deps-used-by: render.c, game/pass_phase.c, main.c
 * @deps-last-changed: 2026-03-19 — Added anim_set_speed(), anim_get_speed()
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "easing.h"
#include "core/card.h"

/* ---- Constants ---- */

#define MAX_CARD_VISUALS 80

#define ANIM_PLAY_CARD_DURATION  0.25f
#define ANIM_PASS_CARD_DURATION  0.4f
#define ANIM_TRICK_COLLECT_DUR   0.3f
#define ANIM_DEAL_CARD_DURATION  0.15f
#define ANIM_DEAL_CARD_STAGGER   0.04f

#define ANIM_TOSS_DURATION      0.35f
#define ANIM_SNAP_BACK_DURATION 0.2f
#define TOSS_VEL_EXTEND         0.2f
#define TOSS_SPIN_FACTOR        0.03f
#define TOSS_SIDEWAYS_FACTOR    0.12f
#define TOSS_MAX_SPIN           720.0f

/* Pass animation timing */
#define ANIM_PASS_TOSS_DURATION    0.45f
#define ANIM_PASS_WAIT_DURATION    0.6f
#define ANIM_PASS_RECEIVE_DURATION 0.35f
#define PASS_TOSS_STAGGER          0.06f  /* delay between cards per player */
#define PASS_PLAYER_STAGGER        0.08f  /* delay between players */

#define HOVER_SCALE_TARGET   1.15f
#define HOVER_LIFT_REF       10.0f
#define HOVER_ANIM_SPEED     8.0f

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
    int      transmute_id;     /* transmutation def ID, -1 = not transmuted */
    float    hover_t;          /* 0.0 = not hovered, 1.0 = fully hovered */
    /* Toss flight (bezier curve animation) */
    bool     use_bezier;
    Vector2  bezier_control;
    float    spin_speed;
} CardVisual;

/* ---- Animation Speed ---- */

/* Set the global animation speed multiplier (called once per frame).
 * 1.0 = normal, 0.5 = fast (half duration), 1.5 = slow. */
void  anim_set_speed(float multiplier);

/* Get current multiplier (for manual timers not driven by anim_start). */
float anim_get_speed(void);

/* ---- Animation API ---- */

/* Start a linear animation from current position to target. */
void anim_start(CardVisual *cv, Vector2 target, float target_rot,
                float duration, EaseType ease);

/* Advance animation by dt seconds. */
void anim_update(CardVisual *cv, float dt);

/* Whether toss animations are enabled. */
bool anim_toss_enabled(void);

/* Set up a bezier toss animation on a card visual.
 * If velocity != NULL, uses it for bezier control (human flick style).
 * If velocity == NULL, synthesizes direction + lateral deviation (AI style).
 * Adds landing scatter, spin, slight rotation offset, and timing variation. */
void anim_setup_toss(CardVisual *cv, Vector2 start_pos, float start_rot,
                     Vector2 target_pos, const Vector2 *velocity,
                     float duration, float delay);

#endif /* ANIM_H */
