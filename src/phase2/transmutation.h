#ifndef TRANSMUTATION_H
#define TRANSMUTATION_H

/* ============================================================
 * @deps-exports: enum TransmuteEffect (TEFFECT_FOG_HIDDEN, TEFFECT_MIRROR),
 *                enum SuitMask, struct TransmuteSlot (transmuter_player),
 *                TrickTransmuteInfo (resolved_effects[])
 * @deps-requires: core/card.h (Card, Suit, Rank, NUM_PLAYERS, MAX_HAND_SIZE)
 * @deps-used-by: transmutation_logic.h, play_phase.c, info_sync.c, update.c,
 *                turn_flow.c, json_parse.c, phase2_state.h, main.c
 * @deps-last-changed: 2026-03-20 — Mirror: added TEFFECT_MIRROR, resolved_effects[]
 * ============================================================ */

#include <stdbool.h>

#include "core/card.h"

/* --- Enums --- */

typedef enum TransmuteEffect {
    TEFFECT_NONE = 0,
    TEFFECT_WOTT_DUPLICATE_ROUND_POINTS,  /* The Martyr */
    TEFFECT_WOTT_REDUCE_SCORE_3,          /* The Gatherer */
    TEFFECT_WOTT_REVEAL_OPPONENT_CARD,    /* The Rogue */
    TEFFECT_WOTT_REDUCE_SCORE_1,          /* The Pendulum */
    TEFFECT_WOTT_SWAP_CARD,               /* The Duel */
    TEFFECT_FOG_HIDDEN,                   /* The Fog: card hidden visually, logic unchanged */
    TEFFECT_MIRROR,                       /* Mirror: copies last globally-played transmutation effect */
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
    int  transmuter_player; /* player who applied, -1 = none */
} TransmuteSlot;

typedef struct HandTransmuteState {
    TransmuteSlot slots[MAX_HAND_SIZE]; /* Parallel to Hand.cards[] */
} HandTransmuteState;

/* --- Per-trick transmutation tracking (parallel to Trick.cards[]) --- */

typedef struct TrickTransmuteInfo {
    int transmutation_ids[CARDS_PER_TRICK]; /* -1 = not transmuted */
    int transmuter_player[CARDS_PER_TRICK]; /* player who applied each transmutation, -1 = none */
    TransmuteEffect resolved_effects[CARDS_PER_TRICK]; /* actual effect after Mirror resolution */
} TrickTransmuteInfo;

/* --- Per-round transmutation effect tracking --- */

typedef struct TransmuteRoundState {
    bool martyr_flags[NUM_PLAYERS]; /* true = this player's round_points doubled at round end */
    int gatherer_reduction[NUM_PLAYERS]; /* Accumulated score reduction (multiples of 3) */
    int rogue_pending_winner; /* Player who won a Rogue trick, -1 = none */
    int duel_pending_winner;  /* Player who won a Duel trick, -1 = none */
} TransmuteRoundState;

#endif /* TRANSMUTATION_H */
