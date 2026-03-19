#ifndef CHARACTER_H
#define CHARACTER_H

/* ============================================================
 * @deps-exports: enum FigureType, struct CharacterDef, MAX_CHARACTER_DEFS,
 *                MAX_CHAR_VENDETTAS
 * @deps-requires: contract.h (CONTRACT_TIERS)
 * @deps-used-by: phase2_defs.h, phase2_defs.c
 * @deps-last-changed: 2026-03-18 — Replaced host_action/revenge with vendetta in Queen/Jack
 * ============================================================ */

#include "contract.h"

/* --- Constants --- */

#define MAX_CHARACTER_DEFS  256
#define MAX_CHAR_VENDETTAS    4

/* --- Figure Type --- */

typedef enum FigureType {
    FIGURE_JACK  = 0,
    FIGURE_QUEEN = 1,
    FIGURE_KING  = 2,
    FIGURE_TYPE_COUNT
} FigureType;

/* --- Character Definition --- */

typedef struct CharacterDef {
    int        id;
    char       name[32];
    char       description[128];
    FigureType figure_type;
    union {
        struct {
            int contract_ids[CONTRACT_TIERS]; /* [0]=easy, [1]=med, [2]=hard */
        } king;
        struct {
            int vendetta_ids[MAX_CHAR_VENDETTAS];
            int num_vendettas;
        } queen;
        struct {
            int _reserved; /* placeholder for future Jack mechanics */
        } jack;
    } mechanics;
    char portrait_asset[32]; /* Character portrait key */
    char card_art_asset[32]; /* Card face art key */
} CharacterDef;

#endif /* CHARACTER_H */
