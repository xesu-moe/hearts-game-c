#ifndef PHASE2_DEFS_H
#define PHASE2_DEFS_H

/* ============================================================
 * @deps-exports: g_contract_defs, g_transmutation_defs,
 *                g_character_defs, g_contract_def_count,
 *                g_transmutation_def_count, g_character_def_count, phase2_defs_init(),
 *                phase2_get_contract(), phase2_get_transmutation(),
 *                phase2_get_character(), phase2_find_transmute_by_effect()
 * @deps-requires: contract.h, transmutation.h (TransmuteEffect), character.h
 * @deps-used-by: contract_logic.c, transmutation_logic.c, json_parse.c,
 *                render.c, pass_phase.c, turn_flow.c, update.c, info_sync.c, main.c
 * @deps-last-changed: 2026-04-04 — Added phase2_find_transmute_by_effect() for Mirror transmutation support
 * ============================================================ */

#include "character.h"
#include "contract.h"
#include "transmutation.h"

/* --- Global Definition Tables --- */

extern ContractDef  g_contract_defs[MAX_CONTRACT_DEFS];
extern int          g_contract_def_count;

extern TransmutationDef g_transmutation_defs[MAX_TRANSMUTATION_DEFS];
extern int              g_transmutation_def_count;

extern CharacterDef g_character_defs[MAX_CHARACTER_DEFS];
extern int          g_character_def_count;

/* Load all definition tables from JSON data files (assets/defs/).
 * Call once at program startup. */
void phase2_defs_init(void);

/* Lookup by id. Returns pointer into global array, or NULL if not found. */
const ContractDef      *phase2_get_contract(int id);
const TransmutationDef *phase2_get_transmutation(int id);
const CharacterDef     *phase2_get_character(int id);

/* Reverse-lookup: find the transmutation ID whose effect matches.
 * Returns -1 if no match (or effect == TEFFECT_NONE / TEFFECT_MIRROR). */
int phase2_find_transmute_by_effect(TransmuteEffect effect);

#endif /* PHASE2_DEFS_H */
