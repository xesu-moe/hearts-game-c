#include "phase2_defs.h"

#include <string.h>

/* ============================================================
 * @deps-implements: phase2_defs.h
 * @deps-last-changed: 2026-03-15 — Initial creation
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
    /* Reset counts */
    g_contract_def_count    = 0;
    g_host_action_def_count = 0;
    g_revenge_def_count     = 0;
    g_character_def_count   = 0;

    /* Zero all tables */
    memset(g_contract_defs,    0, sizeof(g_contract_defs));
    memset(g_host_action_defs, 0, sizeof(g_host_action_defs));
    memset(g_revenge_defs,     0, sizeof(g_revenge_defs));
    memset(g_character_defs,   0, sizeof(g_character_defs));

    /* --- Smoke-test definitions (1 per type) --- */

    /* Contract: "Heartless" — avoid all hearts (easy tier) */
    g_contract_defs[0] = (ContractDef){
        .id          = 0,
        .name        = "Heartless",
        .description = "Don't collect any hearts this round.",
        .condition   = COND_AVOID_SUIT,
        .cond_param  = { .suit = SUIT_HEARTS },
        .num_rewards = 1,
        .rewards     = {{ .type = EFFECT_FLAT_SCORE_ADJUST,
                          .param.points_delta = -3 }},
        .reward_scope = EFFECT_SCOPE_SELF,
        .tier        = 0,
    };
    g_contract_def_count = 1;

    /* Host Action: "Broken Gates" — hearts are broken from the start */
    g_host_action_defs[0] = (HostActionDef){
        .id          = 0,
        .name        = "Broken Gates",
        .description = "Hearts are considered broken from the start of the round.",
        .num_effects = 1,
        .effects     = {{ .type = EFFECT_HEARTS_BREAK_EARLY }},
        .scope       = EFFECT_SCOPE_ALL,
    };
    g_host_action_def_count = 1;

    /* Revenge: "Expose" — reveal the attacker's contract */
    g_revenge_defs[0] = (RevengeDef){
        .id          = 0,
        .name        = "Expose",
        .description = "Reveal the attacker's contract for the rest of the round.",
        .num_effects = 1,
        .effects     = {{ .type = EFFECT_REVEAL_CONTRACT }},
        .scope       = EFFECT_SCOPE_TARGET,
    };
    g_revenge_def_count = 1;

    /* Character: "The Warden" — a sample King with contract bindings */
    g_character_defs[0] = (CharacterDef){
        .id          = 0,
        .name        = "The Warden",
        .description = "A stern keeper of order. Contracts focus on avoidance.",
        .figure_type = FIGURE_KING,
        .mechanics.king.contract_ids = { 0, -1, -1 }, /* Only tier 0 defined */
        .portrait_asset = "warden_portrait",
        .card_art_asset = "warden_card",
    };
    g_character_def_count = 1;
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
