#ifndef PHASE2_STATE_H
#define PHASE2_STATE_H

/* ============================================================
 * @deps-exports: struct RoundPhase2 (suit_seen[], transmute_round),
 *                struct PlayerPhase2, struct KingProgress,
 *                struct Phase2State (last_played_transmute_id, last_played_resolved_effect)
 * @deps-requires: core/card.h (NUM_PLAYERS, SUIT_COUNT),
 *                 contract.h (ContractInstance, CONTRACT_TIERS),
 *                 effect.h (ActiveEffect),
 *                 transmutation.h (TransmuteEffect, TransmuteInventory,
 *                 HandTransmuteState, TransmuteRoundState)
 * @deps-used-by: phase2_defs.h, contract_logic.c, vendetta_logic.h, ai.h,
 *                play_phase.h, pass_phase.h, turn_flow.h, update.h,
 *                info_sync.h, phase_transitions.h, main.c
 * @deps-last-changed: 2026-03-20 — Mirror: added global history fields to Phase2State
 * ============================================================ */

#include <stdbool.h>

#include "core/card.h"
#include "contract.h"
#include "effect.h"
#include "transmutation.h"

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

    /* Transmutation system */
    TransmuteInventory  transmute_inv;      /* Persistent across rounds */
    HandTransmuteState  hand_transmutes;    /* Reset each round */
} PlayerPhase2;

/* --- Per-Round Phase 2 State --- */

typedef struct RoundPhase2 {
    /* Vendetta system (merged host action + revenge) */
    int  vendetta_player_id;   /* -1 = none (round 1 or no vendetta) */
    int  chosen_vendetta;      /* Index into g_vendetta_defs, -1 = none */
    bool vendetta_used;        /* true after vendetta action is spent */
    bool vendetta_chosen;      /* true after player has selected */

    /* Snapshot of previous round's scores (saved before new_round zeroes them) */
    int  prev_round_points[NUM_PLAYERS];

    /* Round-scoped effects from Vendetta + Contracts */
    ActiveEffect round_effects[MAX_ACTIVE_EFFECTS];
    int          num_round_effects;

    bool contracts_chosen;

    /* Per-round suit tracking (has any card of this suit been played) */
    bool suit_seen[SUIT_COUNT];

    /* Transmutation effect flags for this round */
    TransmuteRoundState transmute_round;
} RoundPhase2;

/* --- Top-Level Phase 2 State --- */

typedef struct Phase2State {
    bool         enabled;
    PlayerPhase2 players[NUM_PLAYERS];
    RoundPhase2  round;

    /* Game-scoped Mirror history (persists across rounds) */
    int             last_played_transmute_id;      /* -1 = none played yet */
    TransmuteEffect last_played_resolved_effect;   /* resolved effect (handles Mirror chain) */
} Phase2State;

#endif /* PHASE2_STATE_H */
