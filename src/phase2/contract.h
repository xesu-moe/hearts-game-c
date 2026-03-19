#ifndef CONTRACT_H
#define CONTRACT_H

/* ============================================================
 * @deps-exports: enum ConditionType, struct ConditionParam, struct ContractDef,
 *                struct ContractInstance, MAX_CONTRACT_DEFS, MAX_CONTRACT_REWARD,
 *                MAX_CONTRACT_TRANSMUTE_REWARD, CONTRACT_TIERS
 * @deps-requires: core/card.h (Card, Suit), effect.h (Effect, EffectScope),
 *                 transmutation.h (MAX_TRANSMUTE_INVENTORY)
 * @deps-used-by: character.h, phase2_state.h, phase2_defs.h, phase2_defs.c
 * @deps-last-changed: 2026-03-18 — Added transmutation reward fields to ContractDef
 * ============================================================ */

#include <stdbool.h>

#include "core/card.h"
#include "effect.h"

/* --- Constants --- */

#define MAX_CONTRACT_DEFS            256
#define MAX_CONTRACT_REWARD           2
#define MAX_CONTRACT_TRANSMUTE_REWARD 2
#define CONTRACT_TIERS                3

/* --- Condition Type --- */

typedef enum ConditionType {
    COND_NONE = 0,
    COND_AVOID_SUIT,       /* Don't collect any cards of a suit */
    COND_COLLECT_N_OF_SUIT,/* Collect at least/exactly N cards of a suit */
    COND_WIN_N_TRICKS,     /* Win at least/exactly N tricks */
    COND_TAKE_NO_POINTS,   /* Score 0 points this round */
    COND_TAKE_EXACT_POINTS,/* Score exactly N points */
    COND_AVOID_CARD,       /* Don't take a specific card */
    COND_COLLECT_CARD,     /* Take a specific card */
    COND_WIN_LAST_TRICK,   /* Win trick 13 */
    COND_TYPE_COUNT
} ConditionType;

/* --- Condition Parameters --- */

typedef struct ConditionParam {
    Suit suit;      /* For suit-based conditions */
    int  count;     /* For count-based conditions */
    Card card;      /* For card-specific conditions */
    bool at_least;  /* true = "at least N", false = "exactly N" */
} ConditionParam;

/* --- Contract Definition --- */

typedef struct ContractDef {
    int            id;
    char           name[32];
    char           description[128];
    ConditionType  condition;
    ConditionParam cond_param;
    int            num_rewards;
    Effect         rewards[MAX_CONTRACT_REWARD];
    EffectScope    reward_scope;
    int            tier; /* 0=easy, 1=medium, 2=hard */

    /* Transmutation card rewards (granted on completion) */
    int            transmute_reward_ids[MAX_CONTRACT_TRANSMUTE_REWARD]; /* -1 = none */
    int            num_transmute_rewards;
} ContractDef;

/* --- Contract Instance (per-player, per-round mutable tracker) --- */

typedef struct ContractInstance {
    int  contract_id;     /* Index into g_contract_defs, -1 = none */
    bool revealed;
    bool completed;
    bool failed;
    /* Tracking counters for condition evaluation */
    int  cards_collected[SUIT_COUNT]; /* Cards collected per suit */
    int  tricks_won;
    int  points_taken;
    bool has_card;        /* For COLLECT_CARD / AVOID_CARD conditions */
} ContractInstance;

#endif /* CONTRACT_H */
