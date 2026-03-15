#include "phase2_defs.h"

#include <string.h>

#include "json_parse.h"
#include <raylib.h>

/* ============================================================
 * @deps-implements: phase2_defs.h
 * @deps-requires: phase2_defs.h, json_parse.h, raylib.h
 * @deps-last-changed: 2026-03-15 — Replaced hardcoded defs with JSON loading
 * ============================================================ */

/* --- Global Definition Tables --- */

ContractDef   g_contract_defs[MAX_CONTRACT_DEFS];
int           g_contract_def_count = 0;

HostActionDef g_host_action_defs[MAX_HOST_ACTION_DEFS];
int           g_host_action_def_count = 0;

RevengeDef    g_revenge_defs[MAX_REVENGE_DEFS];
int           g_revenge_def_count = 0;

CharacterDef  g_character_defs[MAX_CHARACTER_DEFS];
int           g_character_def_count = 0;

/* --- Initialization --- */

void phase2_defs_init(void)
{
    /* Zero all tables and counts */
    memset(g_contract_defs,    0, sizeof(g_contract_defs));
    memset(g_host_action_defs, 0, sizeof(g_host_action_defs));
    memset(g_revenge_defs,     0, sizeof(g_revenge_defs));
    memset(g_character_defs,   0, sizeof(g_character_defs));
    g_contract_def_count    = 0;
    g_host_action_def_count = 0;
    g_revenge_def_count     = 0;
    g_character_def_count   = 0;

    /* Load from JSON data files */
    json_load_contracts("assets/defs/contracts.json",
                        g_contract_defs, MAX_CONTRACT_DEFS,
                        &g_contract_def_count);

    json_load_host_actions("assets/defs/host_actions.json",
                           g_host_action_defs, MAX_HOST_ACTION_DEFS,
                           &g_host_action_def_count);

    json_load_revenges("assets/defs/revenges.json",
                       g_revenge_defs, MAX_REVENGE_DEFS,
                       &g_revenge_def_count);

    json_load_characters("assets/defs/characters.json",
                         g_character_defs, MAX_CHARACTER_DEFS,
                         &g_character_def_count);

    TraceLog(LOG_INFO, "PHASE2: Loaded %d contracts, %d host actions, "
             "%d revenges, %d characters",
             g_contract_def_count, g_host_action_def_count,
             g_revenge_def_count, g_character_def_count);
}

/* --- Lookup Functions --- */

const ContractDef *phase2_get_contract(int id)
{
    for (int i = 0; i < g_contract_def_count; i++) {
        if (g_contract_defs[i].id == id) {
            return &g_contract_defs[i];
        }
    }
    return NULL;
}

const HostActionDef *phase2_get_host_action(int id)
{
    for (int i = 0; i < g_host_action_def_count; i++) {
        if (g_host_action_defs[i].id == id) {
            return &g_host_action_defs[i];
        }
    }
    return NULL;
}

const RevengeDef *phase2_get_revenge(int id)
{
    for (int i = 0; i < g_revenge_def_count; i++) {
        if (g_revenge_defs[i].id == id) {
            return &g_revenge_defs[i];
        }
    }
    return NULL;
}

const CharacterDef *phase2_get_character(int id)
{
    for (int i = 0; i < g_character_def_count; i++) {
        if (g_character_defs[i].id == id) {
            return &g_character_defs[i];
        }
    }
    return NULL;
}
