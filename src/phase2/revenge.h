#ifndef REVENGE_H
#define REVENGE_H

/* ============================================================
 * @deps-exports: struct RevengeDef, MAX_REVENGE_DEFS, MAX_REVENGE_EFFECTS
 * @deps-requires: effect.h (Effect, EffectScope)
 * @deps-used-by: phase2_defs.h, phase2_defs.c
 * @deps-last-changed: 2026-03-15 — MAX_REVENGE_DEFS increased from 16 to 64
 * ============================================================ */

#include "effect.h"

/* --- Constants --- */

#define MAX_REVENGE_DEFS    64
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
