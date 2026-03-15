#ifndef REVENGE_H
#define REVENGE_H

/* ============================================================
 * @deps-exports: struct RevengeDef, MAX_REVENGE_DEFS, MAX_REVENGE_EFFECTS
 * @deps-requires: effect.h (Effect, EffectScope)
 * @deps-used-by: phase2_state.h, phase2_defs.h, phase2_defs.c
 * @deps-last-changed: 2026-03-15 — Initial creation
 * ============================================================ */

#include "effect.h"

/* --- Constants --- */

#define MAX_REVENGE_DEFS    16
#define MAX_REVENGE_EFFECTS  2

/* --- Revenge Definition --- */

typedef struct RevengeDef {
    int         id;
    char        name[32];
    char        description[128];
    int         num_effects;
    Effect      effects[MAX_REVENGE_EFFECTS];
    EffectScope scope;
} RevengeDef;

#endif /* REVENGE_H */
