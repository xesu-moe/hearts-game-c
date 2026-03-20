#ifndef TRANSMUTATION_H
#define TRANSMUTATION_H

/* ============================================================
 * @deps-exports: enum TransmuteEffect, enum TransmuteSpecial,
 *                enum SuitMask, struct TransmutationDef,
 *                struct TransmuteInventory, struct TransmuteSlot,
 *                struct HandTransmuteState, struct TrickTransmuteInfo,
 *                struct TransmuteRoundState,
 *                MAX_TRANSMUTATION_DEFS, MAX_TRANSMUTE_INVENTORY
 * @deps-requires: core/card.h (Card, Suit, Rank, NUM_PLAYERS, MAX_HAND_SIZE, CARDS_PER_TRICK)
 * @deps-used-by: phase2_state.h, phase2_defs.h, transmutation_logic.h,
 *                json_parse.h, contract_logic.c, game/play_phase.h
 * @deps-last-changed: 2026-03-20 — Added TEFFECT_WOTT_REDUCE_SCORE_3 to enum, gatherer_reduction field
 * ============================================================ */

#include <stdbool.h>

#include "core/card.h"

/* --- Enums --- */

typedef enum TransmuteEffect {
    TEFFECT_NONE = 0,
    TEFFECT_WOTT_DUPLICATE_ROUND_POINTS,  /* The Martyr */
    TEFFECT_WOTT_REDUCE_SCORE_3,          /* The Gatherer */
    TEFFECT_COUNT
} TransmuteEffect;

typedef enum TransmuteSpecial {
    TRANSMUTE_NORMAL = 0,    /* Standard card, just different suit/rank */
    TRANSMUTE_ALWAYS_WIN,    /* Beats everything in trick resolution */
    TRANSMUTE_ALWAYS_LOSE    /* Loses to everything */
} TransmuteSpecial;

typedef enum SuitMask {
    SUIT_MASK_NONE     = 0,        /* Use result_suit for suit-following */
    SUIT_MASK_CLUBS    = (1 << 0), /* SUIT_CLUBS */
    SUIT_MASK_DIAMONDS = (1 << 1), /* SUIT_DIAMONDS */
    SUIT_MASK_SPADES   = (1 << 2), /* SUIT_SPADES */
    SUIT_MASK_HEARTS   = (1 << 3), /* SUIT_HEARTS */
    SUIT_MASK_ALL      = 0x0F      /* Suitless: can be played anytime */
} SuitMask;

/* --- Constants --- */

#define MAX_TRANSMUTATION_DEFS   64
#define MAX_TRANSMUTE_INVENTORY   8

/* --- TransmutationDef (loaded from JSON, immutable) --- */

typedef struct TransmutationDef {
    int              id;
    char             name[32];
    char             description[128];
    Suit             result_suit;    /* Suit the card becomes */
    Rank             result_rank;    /* Rank the card becomes */
    TransmuteSpecial special;        /* NORMAL / ALWAYS_WIN / ALWAYS_LOSE */
    SuitMask         suit_mask;      /* Override suit-following (0 = use result_suit) */
    int              custom_points;  /* Point value override; -1 = use card_points() */
    bool             negative;       /* AI hint: pass this to opponents */
    TransmuteEffect  effect;         /* Triggered effect (TEFFECT_NONE = no effect) */
    char             art_asset[32];  /* Future: card art key */
} TransmutationDef;

/* --- Per-player persistent inventory (consumed on use) --- */

typedef struct TransmuteInventory {
    int items[MAX_TRANSMUTE_INVENTORY]; /* TransmutationDef IDs, -1 = empty */
    int count;
} TransmuteInventory;

/* --- Per-hand-card transmutation tracking (parallel to Hand.cards[]) --- */

typedef struct TransmuteSlot {
    int  transmutation_id; /* -1 = not transmuted */
    Card original_card;    /* Card before transmutation */
} TransmuteSlot;

typedef struct HandTransmuteState {
    TransmuteSlot slots[MAX_HAND_SIZE]; /* Parallel to Hand.cards[] */
} HandTransmuteState;

/* --- Per-trick transmutation tracking (parallel to Trick.cards[]) --- */

typedef struct TrickTransmuteInfo {
    int transmutation_ids[CARDS_PER_TRICK]; /* -1 = not transmuted */
} TrickTransmuteInfo;

/* --- Per-round transmutation effect tracking --- */

typedef struct TransmuteRoundState {
    bool martyr_flags[NUM_PLAYERS]; /* true = this player's round_points doubled at round end */
    int gatherer_reduction[NUM_PLAYERS]; /* Accumulated score reduction (multiples of 3) */
} TransmuteRoundState;

#endif /* TRANSMUTATION_H */
