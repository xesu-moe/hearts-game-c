#ifndef VENDETTA_H
#define VENDETTA_H

/* ============================================================
 * @deps-exports: enum VendettaTiming, struct VendettaDef,
 *                MAX_VENDETTA_DEFS, MAX_VENDETTA_EFFECTS,
 *                VENDETTA_DISPLAY_NAME
 * @deps-requires: effect.h (Effect, EffectScope)
 * @deps-used-by: phase2_defs.h, vendetta_logic.h, json_parse.h
 * @deps-last-changed: 2026-03-18 — Initial creation (merged host_action + revenge)
 * ============================================================ */

#include "effect.h"

/* --- Constants --- */

#define MAX_VENDETTA_DEFS    64
#define MAX_VENDETTA_EFFECTS  3
#define VENDETTA_DISPLAY_NAME "Vendetta"

/* --- Vendetta Timing --- */

typedef enum VendettaTiming {
    VENDETTA_TIMING_PASSING = 0,
    VENDETTA_TIMING_PLAYING = 1,
    VENDETTA_TIMING_COUNT
} VendettaTiming;

/* --- Vendetta Definition --- */

typedef struct VendettaDef {
    int            id;
    char           name[32];
    char           description[128];
    int            num_effects;
    Effect         effects[MAX_VENDETTA_EFFECTS];
    EffectScope    scope;
    VendettaTiming timing;
} VendettaDef;

#endif /* VENDETTA_H */
