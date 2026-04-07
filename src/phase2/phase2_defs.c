/* ============================================================
 * @deps-implements: phase2_defs.h
 * @deps-requires: phase2_defs.h (phase2_find_transmute_by_effect), transmutation.h (TransmuteEffect),
 *                 json_parse.h (json_parse_contract_defs, json_parse_transmutation_defs), stdio.h, string.h
 * @deps-last-changed: 2026-04-04 — Implemented phase2_find_transmute_by_effect() for Mirror transmutation
 * ============================================================ */

#include "phase2_defs.h"

#include <string.h>
#include <stdio.h>

#include "json_parse.h"

/* --- Global Definition Tables --- */

ContractDef  g_contract_defs[MAX_CONTRACT_DEFS];
int          g_contract_def_count = 0;

TransmutationDef g_transmutation_defs[MAX_TRANSMUTATION_DEFS];
int              g_transmutation_def_count = 0;

CharacterDef g_character_defs[MAX_CHARACTER_DEFS];
int          g_character_def_count = 0;

/* --- Initialization --- */

void phase2_defs_init(void)
{
    /* Zero all tables and counts */
    memset(g_contract_defs,  0, sizeof(g_contract_defs));
    memset(g_transmutation_defs, 0, sizeof(g_transmutation_defs));
    memset(g_character_defs, 0, sizeof(g_character_defs));
    g_contract_def_count      = 0;
    g_transmutation_def_count = 0;
    g_character_def_count     = 0;

    /* Load from JSON data files */
    json_load_contracts("assets/defs/contracts.json",
                        g_contract_defs, MAX_CONTRACT_DEFS,
                        &g_contract_def_count);

    json_load_transmutations("assets/defs/transmutations.json",
                             g_transmutation_defs, MAX_TRANSMUTATION_DEFS,
                             &g_transmutation_def_count);

    fprintf(stderr, "[INFO] PHASE2: Loaded %d contracts, "
                    "%d transmutations\n",
            g_contract_def_count,
            g_transmutation_def_count);
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

const TransmutationDef *phase2_get_transmutation(int id)
{
    for (int i = 0; i < g_transmutation_def_count; i++) {
        if (g_transmutation_defs[i].id == id) {
            return &g_transmutation_defs[i];
        }
    }
    return NULL;
}

int phase2_find_transmute_by_effect(TransmuteEffect effect)
{
    if (effect == TEFFECT_NONE || effect == TEFFECT_MIRROR)
        return -1;
    for (int i = 0; i < g_transmutation_def_count; i++) {
        if (g_transmutation_defs[i].effect == effect)
            return g_transmutation_defs[i].id;
    }
    return -1;
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
