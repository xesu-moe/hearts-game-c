#ifndef PHASE2_DEFS_H
#define PHASE2_DEFS_H

/* ============================================================
 * @deps-exports: g_contract_defs, g_host_action_defs, g_revenge_defs, g_character_defs,
 *                g_contract_def_count, g_host_action_def_count, g_revenge_def_count,
 *                g_character_def_count, phase2_defs_init(),
 *                phase2_get_contract(), phase2_get_host_action(),
 *                phase2_get_revenge(), phase2_get_character()
 * @deps-requires: contract.h, host_action.h, revenge.h, character.h
 * @deps-used-by: (future Phase 2 logic modules)
 * @deps-last-changed: 2026-03-15 — Initial creation
 * ============================================================ */

#include "character.h"
#include "contract.h"
#include "host_action.h"
#include "revenge.h"

/* --- Global Definition Tables --- */

extern ContractDef   g_contract_defs[MAX_CONTRACT_DEFS];
extern int           g_contract_def_count;

extern HostActionDef g_host_action_defs[MAX_HOST_ACTION_DEFS];
extern int           g_host_action_def_count;

extern RevengeDef    g_revenge_defs[MAX_REVENGE_DEFS];
extern int           g_revenge_def_count;

extern CharacterDef  g_character_defs[MAX_CHARACTER_DEFS];
extern int           g_character_def_count;

/* Load all definition tables from JSON data files (assets/defs/).
 * Call once at program startup. */
void phase2_defs_init(void);

/* Lookup by id. Returns pointer into global array, or NULL if not found. */
const ContractDef   *phase2_get_contract(int id);
const HostActionDef *phase2_get_host_action(int id);
const RevengeDef    *phase2_get_revenge(int id);
const CharacterDef  *phase2_get_character(int id);

#endif /* PHASE2_DEFS_H */
