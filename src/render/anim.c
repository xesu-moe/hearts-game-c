/* ============================================================
 * @deps-implements: anim.h
 * @deps-requires: anim.h (CardVisual, anim_set_speed, anim_get_speed, anim_start_scaled),
 *                 easing.h (ease_apply), math.h, stddef.h, raylib.h
 * @deps-last-changed: 2026-03-22 — Scale animation: implemented anim_start_scaled() and scale interpolation in anim_update()
 * ============================================================ */

#include "anim.h"

#include <math.h>
#include <stddef.h>

#include "raylib.h"

/* ---- Global animation speed ---- */

static float g_anim_speed = 1.0f;

void anim_set_speed(float multiplier)
{
    g_anim_speed = multiplier;
}

float anim_get_speed(void)
{
    return g_anim_speed;
}

void anim_start(CardVisual *cv, Vector2 target, float target_rot,
                float duration, EaseType ease)
{
    cv->start = cv->position;
    cv->start_rotation = cv->rotation;
    cv->target = target;
    cv->target_rotation = target_rot;
    cv->anim_elapsed = 0.0f;
    cv->anim_duration = duration * g_anim_speed;
    cv->anim_ease = ease;
    cv->animating = true;
    cv->anim_delay = 0.0f;
    cv->use_bezier = false;
    cv->anim_scale = false;
}

void anim_start_scaled(CardVisual *cv, Vector2 target, float target_rot,
                       float target_scale, float duration, EaseType ease)
{
    cv->start_scale = cv->scale;
    cv->target_scale = target_scale;
    cv->anim_scale = true;
    anim_start(cv, target, target_rot, duration, ease);
    cv->anim_scale = true; /* re-set after anim_start clears it */
}

void anim_update(CardVisual *cv, float dt)
{
    if (!cv->animating) return;

    if (cv->anim_delay > 0.0f) {
        cv->anim_delay -= dt;
        if (cv->anim_delay > 0.0f) return;
        dt = -cv->anim_delay; /* overflow into elapsed */
        cv->anim_delay = 0.0f;
    }

    cv->anim_elapsed += dt;
    if (cv->anim_duration <= 0.0f) {
        cv->position = cv->target;
        cv->rotation = cv->target_rotation;
        if (cv->anim_scale) {
            cv->scale = cv->target_scale;
            cv->anim_scale = false;
        }
        cv->animating = false;
        return;
    }
    float t = cv->anim_elapsed / cv->anim_duration;
    if (t >= 1.0f) {
        t = 1.0f;
        cv->animating = false;
    }

    float eased = ease_apply(cv->anim_ease, t);

    if (cv->use_bezier) {
        /* Quadratic bezier: B(t) = (1-t)^2*P0 + 2(1-t)t*P1 + t^2*P2 */
        float u = 1.0f - eased;
        cv->position.x = u*u*cv->start.x + 2*u*eased*cv->bezier_control.x + eased*eased*cv->target.x;
        cv->position.y = u*u*cv->start.y + 2*u*eased*cv->bezier_control.y + eased*eased*cv->target.y;
        cv->rotation = cv->start_rotation + cv->spin_speed * cv->anim_elapsed;
        if (!cv->animating) cv->use_bezier = false;
    } else {
        cv->position.x = lerpf(cv->start.x, cv->target.x, eased);
        cv->position.y = lerpf(cv->start.y, cv->target.y, eased);
        cv->rotation = lerpf(cv->start_rotation, cv->target_rotation, eased);
    }

    if (cv->anim_scale) {
        cv->scale = lerpf(cv->start_scale, cv->target_scale, eased);
        if (!cv->animating) cv->anim_scale = false;
    }
}

bool anim_toss_enabled(void)
{
    return true;  /* Future: check g_settings.animations_enabled */
}

void anim_setup_toss(CardVisual *cv, Vector2 start_pos, float start_rot,
                     Vector2 target_pos, const Vector2 *velocity,
                     float duration, float delay)
{
    float to_x = target_pos.x - start_pos.x;
    float to_y = target_pos.y - start_pos.y;
    float d = sqrtf(to_x * to_x + to_y * to_y);

    float vx, vy;
    if (velocity != NULL) {
        /* Human flick style: use provided velocity directly */
        vx = velocity->x;
        vy = velocity->y;
    } else {
        /* AI style: synthesize direction with random lateral deviation */
        float speed = (d > 1.0f) ? d / duration : 400.0f;

        /* Two random values averaged for bell-curve-like distribution */
        float lat_a = (float)GetRandomValue(-150, 150);
        float lat_b = (float)GetRandomValue(-150, 150);
        float lateral = (lat_a + lat_b) * 0.5f;

        if (d > 1.0f) {
            float nx = to_x / d, ny = to_y / d;
            vx = nx * speed + (-ny) * lateral;
            vy = ny * speed + nx * lateral;
        } else {
            vx = speed + lateral;
            vy = -speed;
        }
    }

    /* Bezier control point: extend from start along velocity */
    float vel_time = TOSS_VEL_EXTEND;
    Vector2 control = {
        start_pos.x + vx * vel_time,
        start_pos.y + vy * vel_time,
    };

    /* Landing scatter */
    float scatter_x = (float)GetRandomValue(-12, 12);
    float scatter_y = (float)GetRandomValue(-12, 12);
    Vector2 landing = {target_pos.x + scatter_x, target_pos.y + scatter_y};

    cv->position = start_pos;
    cv->start = start_pos;
    cv->target = landing;
    cv->bezier_control = control;
    cv->use_bezier = true;

    /* Spin: sideways cross-product + random component */
    float cross = 0.0f;
    if (d > 1.0f) {
        cross = vx * (to_y / d) - vy * (to_x / d);
    }
    float rnd_a = (float)GetRandomValue(-200, 200);
    float rnd_b = (float)GetRandomValue(-200, 200);
    float spin = cross * TOSS_SIDEWAYS_FACTOR + (rnd_a + rnd_b) * 0.5f;
    if (spin > TOSS_MAX_SPIN) spin = TOSS_MAX_SPIN;
    if (spin < -TOSS_MAX_SPIN) spin = -TOSS_MAX_SPIN;
    cv->spin_speed = spin;

    /* Slight random starting angle offset (±8°) */
    float rot_offset = (float)GetRandomValue(-80, 80) * 0.1f;
    cv->start_rotation = start_rot + rot_offset;
    cv->rotation = start_rot + rot_offset;

    /* Timing variation: ±60ms for natural feel */
    float dur = (duration + (float)GetRandomValue(-60, 60) * 0.001f) * g_anim_speed;
    cv->anim_elapsed = 0.0f;
    cv->anim_duration = dur;
    cv->anim_ease = EASE_OUT_QUAD;
    cv->anim_delay = delay * g_anim_speed;
    cv->animating = true;
}
