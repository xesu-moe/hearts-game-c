/* ============================================================
 * @deps-implements: easing.h
 * @deps-requires: easing.h
 * @deps-last-changed: 2026-03-19 — Renamed from anim.c
 * ============================================================ */

#include "easing.h"

float ease_apply(EaseType type, float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    switch (type) {
    case EASE_LINEAR:
        return t;

    case EASE_IN_QUAD:
        return t * t;

    case EASE_OUT_QUAD:
        return t * (2.0f - t);

    case EASE_IN_OUT_QUAD:
        if (t < 0.5f) return 2.0f * t * t;
        return -1.0f + (4.0f - 2.0f * t) * t;

    case EASE_OUT_BACK: {
        float c1 = 1.70158f;
        float c3 = c1 + 1.0f;
        float tm1 = t - 1.0f;
        return 1.0f + c3 * tm1 * tm1 * tm1 + c1 * tm1 * tm1;
    }

    default:
        return t;
    }
}

float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}
