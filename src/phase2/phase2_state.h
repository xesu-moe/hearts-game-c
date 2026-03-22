#ifndef PHASE2_STATE_H
#define PHASE2_STATE_H

/* ============================================================
 * @deps-exports: struct DraftPair, struct DraftPlayerState, struct DraftState,
 *                struct PlayerPhase2, struct RoundPhase2, struct Phase2State,
 *                MAX_ACTIVE_CONTRACTS, DRAFT_GROUP_SIZE, DRAFT_PICKS_PER_PLAYER
 * @deps-requires: core/card.h (NUM_PLAYERS, SUIT_COUNT), contract.h (ContractInstance),
 *                 effect.h (ActiveEffect), transmutation.h (TransmuteEffect, TransmuteInventory, HandTransmuteState, TransmuteRoundState)
 * @deps-used-by: contract_logic.c, pass_phase.c, update.c, info_sync.c, turn_flow.c
 * @deps-last-changed: 2026-03-21 — Added DraftState and draft in RoundPhase2, replaced king_ids with contracts[MAX_ACTIVE_CONTRACTS] array
 * ============================================================ */

#include <stdbool.h>

#include "core/card.h"
#include "contract.h"
#include "effect.h"
#include "transmutation.h"

/* ---- Draft types ---- */

#define DRAFT_GROUP_SIZE       4
#define DRAFT_PICKS_PER_PLAYER 3
#define MAX_ACTIVE_CONTRACTS   3

typedef struct DraftPair {
    int contract_id;      /* -1 = empty/discarded */
    int transmutation_id; /* -1 = none */
} DraftPair;

typedef struct DraftPlayerState {
    DraftPair available[DRAFT_GROUP_SIZE];
    int       available_count;            /* 4, 3, 2 per round */
    DraftPair picked[DRAFT_PICKS_PER_PLAYER];
    int       pick_count;                 /* 0..3 */
    bool      has_picked_this_round;
} DraftPlayerState;

typedef struct DraftState {
    DraftPlayerState players[NUM_PLAYERS];
    int              current_round;  /* 0, 1, 2 */
    bool             active;
    float            timer;
} DraftState;

/* --- Per-Player Phase 2 State --- */

typedef struct PlayerPhase2 {
    /* Character assignments (character_id per suit) */
    int queen_ids[SUIT_COUNT];
    int jack_ids[SUIT_COUNT];

    /* Current round's active contracts (up to 3 from draft) */
    ContractInstance contracts[MAX_ACTIVE_CONTRACTS];
    int              num_active_contracts;

    /* Persistent effects from completed contracts */
    ActiveEffect persistent_effects[MAX_ACTIVE_EFFECTS];
    int          num_persistent;

    /* Transmutation system */
    TransmuteInventory  transmute_inv;      /* Persistent across rounds */
    HandTransmuteState  hand_transmutes;    /* Reset each round */
} PlayerPhase2;

/* --- Per-Round Phase 2 State --- */

typedef struct RoundPhase2 {
    /* Snapshot of previous round's scores (saved before new_round zeroes them) */
    int  prev_round_points[NUM_PLAYERS];

    bool contracts_chosen;

    /* Per-round suit tracking (has any card of this suit been played) */
    bool suit_seen[SUIT_COUNT];

    /* Transmutation effect flags for this round */
    TransmuteRoundState transmute_round;

    /* Draft state (managed by contract_logic.c) */
    DraftState draft;
} RoundPhase2;

/* --- Top-Level Phase 2 State --- */

typedef struct Phase2State {
    bool         enabled;
    PlayerPhase2 players[NUM_PLAYERS];
    RoundPhase2  round;

    /* Game-scoped Mirror history (persists across rounds) */
    int             last_played_transmute_id;      /* -1 = none played yet */
    TransmuteEffect last_played_resolved_effect;   /* resolved effect (handles Mirror chain) */

    /* Game-scoped Shield countdown (persists across rounds) */
    int shield_tricks_remaining[NUM_PLAYERS]; /* 0 = inactive, 1-3 = tricks of 0-point protection */

    /* Curse: force lead hearts on next trick */
    bool curse_force_hearts[NUM_PLAYERS]; /* true = must lead a heart next trick */

    /* Anchor: forced lead suit for next trick (-1 = inactive) */
    int anchor_force_suit[NUM_PLAYERS];

    /* Binding: auto-win next trick (0 = inactive, 1 = active) */
    int binding_auto_win[NUM_PLAYERS];
} Phase2State;

#endif /* PHASE2_STATE_H */
