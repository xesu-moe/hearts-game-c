#ifndef PHASE2_STATE_H
#define PHASE2_STATE_H

/* ============================================================
 * @deps-exports: struct KingProgress, struct GrudgeToken, struct PlayerPhase2,
 *                struct RoundPhase2, struct Phase2State
 * @deps-requires: core/card.h (NUM_PLAYERS, SUIT_COUNT),
 *                 effect.h (ActiveEffect), contract.h (ContractInstance, CONTRACT_TIERS)
 * @deps-used-by: phase2_defs.h, contract_logic.c, grudge_logic.h, main.c
 * @deps-last-changed: 2026-03-16 — Added GrudgeToken struct to PlayerPhase2
 * ============================================================ */

#include <stdbool.h>

#include "core/card.h"
#include "contract.h"
#include "effect.h"

/* --- King Progress (per-King contract progression) --- */

typedef struct KingProgress {
    int  current_tier;         /* 0=easy, 1=medium, 2=hard, 3=exhausted */
    bool tier_completed[CONTRACT_TIERS]; /* true=completed, false=burned or unattempted */
} KingProgress;

/* --- Grudge Token (persistent revenge trigger) --- */

typedef struct GrudgeToken {
    bool active;          /* true = player holds a token */
    int  attacker_id;     /* who played QoS against us */
    bool used_this_round; /* skip re-prompting after decline */
} GrudgeToken;

/* --- Per-Player Phase 2 State --- */

typedef struct PlayerPhase2 {
    /* Character assignments (character_id per suit) */
    int king_ids[SUIT_COUNT];
    int queen_ids[SUIT_COUNT];
    int jack_ids[SUIT_COUNT];

    /* Contract progression per suit's King */
    KingProgress king_progress[SUIT_COUNT];

    /* Current round's active contract */
    ContractInstance contract;

    /* Persistent effects from completed contracts */
    ActiveEffect persistent_effects[MAX_ACTIVE_EFFECTS];
    int          num_persistent;

    /* Grudge token (replaces old revenge fields) */
    GrudgeToken grudge_token;
} PlayerPhase2;

/* --- Per-Round Phase 2 State --- */

typedef struct RoundPhase2 {
    int  host_player_id;      /* -1 = none (round 1 or vanilla) */
    int  chosen_host_action;  /* Index into g_host_action_defs, -1 = none */

    /* Round-scoped effects from Host action + Revenges */
    ActiveEffect round_effects[MAX_ACTIVE_EFFECTS];
    int          num_round_effects;

    bool contracts_chosen;
    bool host_action_chosen;
} RoundPhase2;

/* --- Top-Level Phase 2 State --- */

typedef struct Phase2State {
    bool         enabled;
    PlayerPhase2 players[NUM_PLAYERS];
    RoundPhase2  round;
} Phase2State;

#endif /* PHASE2_STATE_H */
