#ifndef ANIM_H
#define ANIM_H

/* ============================================================
 * @deps-exports: struct CardVisual, anim_start_scaled(), ANIM_PASS_HAND_SLIDE_DURATION, ANIM_PASS_RECEIVE_GAP_DELAY
 * @deps-requires: easing.h (EaseType), raylib.h (Vector2), core/card.h (Card), stdint.h
 * @deps-used-by: render.c, anim.c, game/pass_phase.c, game/turn_flow.c, game/update.c
 * @deps-last-changed: 2026-04-05 — Added scoring_hidden field to CardVisual for conditionally hiding untriggered Trap cards in scoring screen
 * ============================================================ */

#include <stdbool.h>
#include <stdint.h>

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

/* Pile collect animation */
#define ANIM_PILE_COLLECT_DURATION 0.35f
#define ANIM_PILE_STAGGER          0.06f  /* delay between each of the 4 cards */

/* Pass animation timing */
#define ANIM_PASS_TOSS_DURATION       0.45f
#define ANIM_PASS_WAIT_DURATION       0.6f
#define ANIM_PASS_REVEAL_FLY_DURATION 0.4f
#define ANIM_PASS_RECEIVE_DURATION    0.35f
#define ANIM_PASS_HAND_SLIDE_DURATION 0.35f  /* hand cards sliding to close/open gaps
                                              (must be <= ANIM_PASS_TOSS_DURATION) */
#define ANIM_PASS_RECEIVE_GAP_DELAY   0.25f  /* delay before receive fly-in (gaps open first) */
#define PASS_TOSS_STAGGER          0.06f  /* delay between cards per player */
#define PASS_PLAYER_STAGGER        0.08f  /* delay between players */

/* Scoring phase animation */
#define ANIM_SCORING_FLY_DURATION    0.5f
#define ANIM_SCORING_PLAYER_STAGGER  0.2f
#define ANIM_SCORING_CARD_STAGGER    0.06f
#define ANIM_SCORING_MENU_DELAY      0.2f
#define ANIM_SCORING_MENU_DURATION   0.35f
#define ANIM_SCORING_COUNTUP_RATE    0.08f
#define ANIM_CONTRACT_REVEAL_STAGGER 0.4f

#define ANIM_EFFECT_FLIGHT_DURATION  0.35f  /* card to/from center (Rogue/Duel) */
#define ANIM_DUEL_EXCHANGE_DURATION  0.40f  /* simultaneous swap flight */

#define ANIM_REARRANGE_BLEND_RATE  12.0f

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
    /* Scale animation */
    float    start_scale;
    float    target_scale;
    bool     anim_scale;      /* whether this animation interpolates scale */
    /* Toss flight (bezier curve animation) */
    bool     use_bezier;
    Vector2  bezier_control;
    float    spin_speed;
    int      pile_owner;       /* player who won this pile card, -1 = unset */
    uint8_t  revealed_to;     /* per-player visibility bitmask (bit N = visible to player N) */
    uint8_t  fog_mode;        /* 0 = no fog, 1 = semi-transparent (owner), 2 = opaque */
    float    fog_reveal_t;    /* 1.0 = fully fogged, 0.0 = revealed; animated on trick resolve */
    bool     dimmed;          /* true = draw dark overlay (unplayable card) */
    bool     shielded;        /* true = points negated by Shield effect */
    bool     inverted;        /* true = points negated by Inversion (down arrow) */
    bool     scoring_hidden;  /* true = hide from scoring screen (e.g. untriggered Trap) */
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

/* Start animation with scale interpolation (position + rotation + scale). */
void anim_start_scaled(CardVisual *cv, Vector2 target, float target_rot,
                       float target_scale, float duration, EaseType ease);

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
