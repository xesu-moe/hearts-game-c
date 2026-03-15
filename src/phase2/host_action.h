#ifndef HOST_ACTION_H
#define HOST_ACTION_H

/* ============================================================
 * @deps-exports: struct HostActionDef, MAX_HOST_ACTION_DEFS, MAX_HOST_EFFECTS
 * @deps-requires: effect.h (Effect, EffectScope)
 * @deps-used-by: phase2_defs.h, phase2_defs.c
 * @deps-last-changed: 2026-03-15 — MAX_HOST_ACTION_DEFS increased from 16 to 64
 * ============================================================ */

#include "effect.h"

/* --- Constants --- */

#define MAX_HOST_ACTION_DEFS 64
#define MAX_HOST_EFFECTS      3

/* --- Host Action Definition --- */

typedef struct HostActionDef {
    int         id;
    char        name[32];
    char        description[128];
    int         num_effects;
    Effect      effects[MAX_HOST_EFFECTS];
    EffectScope scope;
} HostActionDef;

#endif /* HOST_ACTION_H */
