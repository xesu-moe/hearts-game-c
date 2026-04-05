/* ============================================================
 * @deps-exports: CompetitiveAIState, QSStatus (enum), AIWeights,
 *                AIThresholds, comp_ai_init_hand(), comp_ai_on_trick_complete(),
 *                comp_ai_on_pass_execute(), comp_ai_pick_direction(),
 *                comp_ai_pick_amount(), comp_ai_draft_pick(),
 *                comp_ai_select_pass(), comp_ai_play_card(),
 *                comp_ai_apply_transmutations(), comp_ai_rogue_pick(*out_suit),
 *                comp_ai_duel_pick()
 * @deps-requires: core/card.h (Card, DECK_SIZE, SUIT_COUNT, NUM_PLAYERS),
 *                 core/game_state.h (GameState, PassDirection, MAX_PASS_CARD_COUNT),
 *                 phase2/phase2_state.h (Phase2State, MAX_ACTIVE_CONTRACTS),
 *                 phase2/contract.h (DraftPlayerState),
 *                 phase2/transmutation.h (TransmuteInventory, HandTransmuteState)
 * @deps-used-by: server_game.h, server_game.c, ai_competitive.c
 * @deps-last-changed: 2026-04-04 — Changed comp_ai_rogue_pick signature: *out_idx → *out_suit for Rogue suit redesign
 * ============================================================ */

#ifndef AI_COMPETITIVE_H
#define AI_COMPETITIVE_H

#include <stdbool.h>
#include "core/card.h"
#include "core/game_state.h"
#include "phase2/phase2_state.h"
#include "phase2/contract.h"
#include "phase2/transmutation.h"

/* Forward declarations */
struct ServerGame;
struct DraftPlayerState;
struct TrickTransmuteInfo;

/* ================================================================
 * Queen of Spades Tracking
 * ================================================================ */

typedef enum QSStatus {
    QS_IN_MY_HAND,
    QS_PLAYED,
    QS_PASSED_TO,
    QS_RECEIVED_FROM,
    QS_LOCATION_UNKNOWN
} QSStatus;

/* ================================================================
 * Per-AI-Player, Per-Hand State
 * ================================================================ */

typedef struct CompetitiveAIState {
    int my_seat;

    /* 1.1 — Played cards */
    bool played[DECK_SIZE];

    /* 1.2 — Void map: player_void[player][suit] */
    bool player_void[NUM_PLAYERS][SUIT_COUNT];

    /* 1.3 — Queen of Spades tracking */
    QSStatus qs_status;
    int qs_related_player;  /* for PASSED_TO / RECEIVED_FROM */

    /* 1.4 — Remaining cards per suit (starts at 13) */
    int remaining_in_suit[SUIT_COUNT];

    /* 1.5 — Points taken this hand */
    int hearts_taken[NUM_PLAYERS];
    bool has_queen[NUM_PLAYERS];

    /* 1.6 — Cumulative game scores */
    int game_score[NUM_PLAYERS];

    /* 1.7 — Pass analysis */
    Card cards_passed[MAX_PASS_CARD_COUNT];
    Card cards_received[MAX_PASS_CARD_COUNT];
    int num_passed;
    int num_received;
    PassDirection pass_direction;

    /* Moon state */
    bool moon_alarm;
    int moon_suspect;       /* player id, -1 = none */
    bool attempting_moon;

    /* Contract awareness */
    int active_contract_ids[MAX_ACTIVE_CONTRACTS];
    int num_active_contracts;
    bool contract_abandoned[MAX_ACTIVE_CONTRACTS];
} CompetitiveAIState;

/* ================================================================
 * Tunable Weights (Section 10.3 of COMPETITIVE_AI.md)
 * ================================================================ */

typedef struct AIWeights {
    float w_safety[3];    /* [early, mid, late] */
    float w_void[3];
    float w_shed[3];
    float w_queen[3];
    float w_moon[3];
    float w_target[3];
    float w_info[3];
    float w_endgame[3];
} AIWeights;

typedef struct AIThresholds {
    int moon_score_threshold;       /* 14 — min to consider shooting */
    int moon_commit_threshold;      /* 18 — min to fully commit */
    int moon_alarm_with_queen;      /* 5  — hearts + QS triggers alarm */
    int moon_alarm_no_queen;        /* 8  — hearts alone triggers alarm */
    int spade_buffer_safe;          /* 4  — spades to safely hold QS */
    int spade_buffer_min;           /* 3  — min spades to consider keeping QS */
    int low_heart_max_rank;         /* 6  — ranks 2-6 are "low hearts" */
    int endgame_start_trick;        /* 10 — when endgame logic activates */
    int desperate_score_gap;        /* 15 — gap to trigger desperation */
    int cooperative_moon_lead;      /* 30 — score lead to allow opponent moon */
} AIThresholds;

/* ================================================================
 * Lifecycle
 * ================================================================ */

/* Reset per-hand state. Call at the start of each round after dealing. */
void comp_ai_init_hand(CompetitiveAIState *ai, int seat,
                       const GameState *gs);

/* Update tracking after a trick resolves. */
void comp_ai_on_trick_complete(CompetitiveAIState *ai,
                               const Trick *trick, int winner);

/* Update tracking after pass cards are exchanged. */
void comp_ai_on_pass_execute(CompetitiveAIState *ai,
                             const Card *sent, int sent_count,
                             const Card *received, int recv_count,
                             PassDirection dir, int pass_target);

/* ================================================================
 * Decision Functions
 * ================================================================ */

/* Dealer phase: pick pass direction strategically. */
PassDirection comp_ai_pick_direction(const CompetitiveAIState *ai,
                                     const GameState *gs);

/* Dealer phase: pick pass amount (2, 3, or 4). */
int comp_ai_pick_amount(const CompetitiveAIState *ai,
                        const GameState *gs);

/* Draft phase: pick contract+transmutation pair.
 * Returns index into dps->available[]. */
int comp_ai_draft_pick(const DraftPlayerState *dps,
                       const Phase2State *p2);

/* Pass phase: select cards to pass.
 * Fills out_cards with pass_count cards. */
void comp_ai_select_pass(CompetitiveAIState *ai,
                         const GameState *gs,
                         Card *out_cards, int pass_count);

/* Play phase: choose the best card to play.
 * Returns the chosen card. */
Card comp_ai_play_card(CompetitiveAIState *ai,
                       const struct ServerGame *sg, int player_id);

/* Transmutation phase: apply transmutations strategically. */
void comp_ai_apply_transmutations(CompetitiveAIState *ai,
                                  Hand *hand,
                                  HandTransmuteState *hts,
                                  TransmuteInventory *inv,
                                  bool is_passing, int player_id);

/* Rogue effect: pick target player and suit to reveal. */
void comp_ai_rogue_pick(const CompetitiveAIState *ai,
                        const GameState *gs, int winner,
                        int *out_target, int *out_suit);

/* Duel effect: pick target, their card, and own card to give. */
void comp_ai_duel_pick(const CompetitiveAIState *ai,
                       const GameState *gs, const Phase2State *p2,
                       int winner,
                       int *out_target, int *out_target_idx,
                       int *out_give_idx);

#endif /* AI_COMPETITIVE_H */
