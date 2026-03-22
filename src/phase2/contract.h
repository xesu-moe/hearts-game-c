#ifndef CONTRACT_H
#define CONTRACT_H

/* ============================================================
 * @deps-exports: enum ConditionType, struct ConditionParam, struct ContractDef,
 *                struct ContractInstance (paired_transmutation_id)
 * @deps-requires: core/card.h (Card, Suit, SUIT_COUNT),
 *                 core/game_state.h (PASS_CARD_COUNT), effect.h (Effect)
 * @deps-used-by: phase2_state.h, contract_logic.c, phase2_defs.c
 * @deps-last-changed: 2026-03-21 — Added paired_transmutation_id to ContractInstance
 * ============================================================ */

#include <stdbool.h>
#include <stdint.h>

#include "core/card.h"
#include "core/game_state.h" /* PASS_CARD_COUNT */
#include "effect.h"

/* --- Constants --- */

#define MAX_CONTRACT_DEFS            256
#define MAX_CONTRACT_REWARD           2
#define MAX_CONTRACT_TRANSMUTE_REWARD 2
#define CONTRACT_TIERS                3

/* --- Condition Type --- */

typedef enum ConditionType {
    COND_NONE = 0,
    COND_AVOID_SUIT,              /* Don't collect any cards of a suit */
    COND_COLLECT_N_OF_SUIT,       /* Collect at least/exactly N cards of a suit */
    COND_WIN_N_TRICKS,            /* Win at least/exactly N tricks */
    COND_TAKE_NO_POINTS,          /* Score 0 points this round */
    COND_TAKE_EXACT_POINTS,       /* Score exactly N points */
    COND_AVOID_CARD,              /* Don't take a specific card */
    COND_COLLECT_CARD,            /* Take a specific card */
    COND_WIN_CONSECUTIVE_TRICKS,  /* Win N consecutive tricks */
    COND_HIT_N_WITH_SUIT,         /* Hit N times with suit X */
    COND_LOWEST_SCORE,            /* Strictly lowest round score */
    COND_NEVER_LEAD_SUIT,         /* Never lead with suit X */
    COND_WIN_TRICK_N,             /* Win specific trick number */
    COND_BREAK_HEARTS,            /* Be the player who breaks hearts */
    COND_WIN_FIRST_N_TRICKS,      /* Win first N tricks */
    COND_AVOID_LAST_N_TRICKS,     /* Don't win any of last N tricks */
    COND_WIN_WITH_PASSED_CARD,    /* Win trick using a passed card */
    COND_HIT_WITH_PASSED_CARD,    /* Hit with a passed card */
    COND_WIN_FIRST_AND_LAST,      /* Win both trick 0 and trick 12 */
    COND_LEAD_QUEEN_SPADES_TRICK, /* Lead the trick where QoS is played */
    COND_SHOOT_THE_MOON,          /* Collect all 26 points */
    COND_PREVENT_MOON,            /* Break another player's moon run */
    COND_PLAY_CARD_FIRST_OF_SUIT, /* Play specific card as first of its suit */
    COND_HIT_WITH_TRANSMUTE,      /* Hit with a scoring transmutation card */
    COND_TYPE_COUNT
} ConditionType;

/* --- Condition Parameters --- */

typedef struct ConditionParam {
    Suit suit;      /* For suit-based conditions */
    int  count;     /* For count-based conditions */
    Card card;      /* For card-specific conditions */
    bool at_least;  /* true = "at least N", false = "exactly N" */
    int  trick_num; /* For WIN_TRICK_N, WIN_FIRST_N, AVOID_LAST_N */
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

    /* Trick-number tracking */
    uint16_t tricks_won_mask;      /* bit i = won trick i (0-12) */
    int      current_streak;       /* current consecutive wins */
    int      max_streak;           /* best consecutive win streak */

    /* Lead/play tracking */
    bool     led_with_suit[SUIT_COUNT];
    bool     broke_hearts;

    /* Specific-trick tracking */
    bool     led_qs_trick;
    bool     played_card_first_of_suit;

    /* Passed-card tracking */
    Card     received_in_pass[PASS_CARD_COUNT];
    int      num_received;
    bool     won_with_passed_card;
    bool     hit_with_passed_card;

    /* Hit tracking */
    int      hits_dealt[SUIT_COUNT]; /* scoring cards into lost tricks */

    /* Moon tracking */
    bool     prevented_moon;

    /* Transmutation tracking */
    bool     hit_with_transmute;

    /* Draft-paired transmutation reward */
    int      paired_transmutation_id; /* -1 = none */
} ContractInstance;

#endif /* CONTRACT_H */
