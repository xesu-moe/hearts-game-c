#ifndef EFFECT_H
#define EFFECT_H

/* ============================================================
 * @deps-exports: enum EffectType, struct Effect, enum EffectScope, struct ActiveEffect
 * @deps-requires: core/card.h (Suit)
 * @deps-used-by: contract.h, phase2_state.h, phase2_defs.h
 * @deps-last-changed: 2026-03-15 — Initial creation
 * ============================================================ */

#include <stdbool.h>

#include "core/card.h"

/* --- Effect Type --- */

typedef enum EffectType {
    EFFECT_NONE = 0,
    EFFECT_POINTS_PER_HEART,     /* Modify point value of each heart */
    EFFECT_POINTS_FOR_QOS,       /* Modify point value of Queen of Spades */
    EFFECT_FLAT_SCORE_ADJUST,    /* Flat +/- to round score */
    EFFECT_HEARTS_BREAK_EARLY,   /* Hearts are considered broken from start */
    EFFECT_FORCE_PASS_DIRECTION, /* Override pass direction for the round */
    EFFECT_VOID_SUIT,            /* A suit scores 0 points */
    EFFECT_REVEAL_HAND,          /* Target's hand is visible */
    EFFECT_REVEAL_CONTRACT,      /* Target's contract is revealed */
    EFFECT_SWAP_CARD_POINTS,     /* Hearts and QoS swap point values */
    EFFECT_TYPE_COUNT
} EffectType;

/* --- Effect --- */

typedef struct Effect {
    EffectType type;
    union {
        int points_delta;   /* POINTS_PER_HEART, POINTS_FOR_QOS, FLAT_SCORE_ADJUST */
        Suit voided_suit;   /* VOID_SUIT */
        int pass_direction; /* FORCE_PASS_DIRECTION (cast to PassDirection) */
    } param;
} Effect;

/* --- Effect Scope --- */

typedef enum EffectScope {
    EFFECT_SCOPE_SELF,      /* Affects only the source player */
    EFFECT_SCOPE_TARGET,    /* Affects a specific target player */
    EFFECT_SCOPE_ALL,       /* Affects all players */
    EFFECT_SCOPE_OPPONENTS  /* Affects all players except source */
} EffectScope;

/* --- Active Effect (runtime instance) --- */

#define MAX_ACTIVE_EFFECTS 8

typedef struct ActiveEffect {
    Effect      effect;
    EffectScope scope;
    int         source_player;    /* Player who created the effect */
    int         target_player;    /* For TARGET scope, -1 otherwise */
    int         rounds_remaining; /* -1 = permanent, 0 = expired */
    bool        active;
} ActiveEffect;

#endif /* EFFECT_H */
