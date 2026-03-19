/* ============================================================
 * @deps-implements: anim.h
 * @deps-requires: anim.h, easing.h
 * @deps-last-changed: 2026-03-19 — Extracted from render.c
 * ============================================================ */

#include "anim.h"

void anim_start(CardVisual *cv, Vector2 target, float target_rot,
                float duration, EaseType ease)
{
    cv->start = cv->position;
    cv->start_rotation = cv->rotation;
    cv->target = target;
    cv->target_rotation = target_rot;
    cv->anim_elapsed = 0.0f;
    cv->anim_duration = duration;
    cv->anim_ease = ease;
    cv->animating = true;
    cv->anim_delay = 0.0f;
    cv->use_bezier = false;
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
}

bool anim_toss_enabled(void)
{
    return true;  /* Future: check g_settings.animations_enabled */
}
