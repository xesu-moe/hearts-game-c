/* ============================================================
 * @deps-implements: phase2_defs.h
 * @deps-requires: phase2_defs.h, transmutation.h (TransmuteEffect), json_parse.h, stdio.h, string.h
 * @deps-last-changed: 2026-03-22 — Removed raylib.h, replaced TraceLog with fprintf
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

    json_load_characters("assets/defs/characters.json",
                         g_character_defs, MAX_CHARACTER_DEFS,
                         &g_character_def_count);

    fprintf(stderr, "[INFO] PHASE2: Loaded %d contracts, "
                    "%d transmutations, %d characters\n",
            g_contract_def_count,
            g_transmutation_def_count, g_character_def_count);
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

const CharacterDef *phase2_get_character(int id)
{
    for (int i = 0; i < g_character_def_count; i++) {
        if (g_character_defs[i].id == id) {
            return &g_character_defs[i];
        }
    }
    return NULL;
}
