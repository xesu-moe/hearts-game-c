#ifndef PHASE2_STATE_H
#define PHASE2_STATE_H

/* ============================================================
 * @deps-exports: struct KingProgress, struct PlayerPhase2, struct RoundPhase2,
 *                struct Phase2State
 * @deps-requires: card.h (NUM_PLAYERS, SUIT_COUNT), effect.h (ActiveEffect),
 *                 contract.h (ContractInstance, CONTRACT_TIERS)
 * @deps-used-by: phase2_defs.h, phase2_defs.c
 * @deps-last-changed: 2026-03-15 — Initial creation
 * ============================================================ */

#include <stdbool.h>

#include "card.h"
#include "contract.h"
#include "effect.h"

/* --- King Progress (per-King contract progression) --- */

typedef struct KingProgress {
    int  current_tier;         /* 0=easy, 1=medium, 2=hard, 3=exhausted */
    bool tier_completed[CONTRACT_TIERS]; /* true=completed, false=burned or unattempted */
} KingProgress;

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

    /* Revenge state */
    bool has_revenge_available;
    int  qos_attacker_id;   /* Player who played QoS against us, -1 = none */
    int  chosen_revenge_id; /* Index into g_revenge_defs, -1 = none */
    bool revenge_used;
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
