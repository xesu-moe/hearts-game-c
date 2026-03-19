#ifndef EASING_H
#define EASING_H

/* ============================================================
 * @deps-exports: EaseType, ease_apply(), lerpf()
 * @deps-requires: (none — leaf module)
 * @deps-used-by: render/anim.h
 * @deps-last-changed: 2026-03-19 — Renamed from anim.h
 * ============================================================ */

typedef enum EaseType {
    EASE_LINEAR,
    EASE_IN_QUAD,
    EASE_OUT_QUAD,
    EASE_IN_OUT_QUAD,
    EASE_OUT_BACK,
    EASE_COUNT
} EaseType;

/* Apply easing function. t in [0,1] -> eased value in [0,1]. */
float ease_apply(EaseType type, float t);

/* Linear interpolation between a and b by t. */
float lerpf(float a, float b, float t);

#endif /* EASING_H */
