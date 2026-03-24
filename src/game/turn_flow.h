#ifndef TURN_FLOW_H
#define TURN_FLOW_H

/* ============================================================
 * @deps-exports: enum FlowStep (FLOW_TRICK_DISPLAY, FLOW_FOG_REVEAL,
 *                FLOW_TRICK_PILE_ANIM, FLOW_ROGUE_ANIM_TO_CENTER,
 *                FLOW_ROGUE_ANIM_BACK, FLOW_DUEL_ANIM_TO_CENTER,
 *                FLOW_DUEL_ANIM_EXCHANGE, FLOW_DUEL_ANIM_RETURN),
 *                struct TurnFlow, FLOW_FOG_REVEAL_TIME, FLOW_FOG_DISSOLVE_TIME
 * @deps-requires: core/game_state.h (GameState, CARDS_PER_TRICK),
 *                 core/settings.h (GameSettings), phase2/phase2_state.h,
 *                 play_phase.h (PlayPhaseState)
 * @deps-used-by: turn_flow.c, process_input.c, phase_transitions.c, main.c
 * @deps-last-changed: 2026-03-23 — Step 10: Added bool online parameter to flow_update
 *                     timing constants for fog reveal transition
 * ============================================================ */

#include "core/game_state.h"
#include "core/settings.h"
#include "phase2/phase2_state.h"
#include "game/play_phase.h"

/* Forward declarations */
typedef struct RenderState RenderState;

typedef enum FlowStep {
    FLOW_IDLE,
    FLOW_WAITING_FOR_HUMAN,
    FLOW_AI_THINKING,
    FLOW_CARD_ANIMATING,
    FLOW_TRICK_DISPLAY,
    FLOW_FOG_REVEAL,
    FLOW_TRICK_PILE_ANIM,
    FLOW_TRICK_COLLECTING,
    FLOW_ROGUE_CHOOSING,
    FLOW_ROGUE_ANIM_TO_CENTER,
    FLOW_ROGUE_REVEAL,
    FLOW_ROGUE_ANIM_BACK,
    FLOW_DUEL_PICK_OPPONENT,
    FLOW_DUEL_ANIM_TO_CENTER,
    FLOW_DUEL_PICK_OWN,
    FLOW_DUEL_ANIM_EXCHANGE,
    FLOW_DUEL_ANIM_RETURN,
    FLOW_BETWEEN_TRICKS,
} FlowStep;

typedef struct TurnFlow {
    FlowStep step;
    float    timer;
    float    turn_timer;
    int      animating_player;
    int      prev_trick_count;
    bool     hearts_broken_at_trick_start;
    int      rogue_winner;          /* player who won the Rogue trick, -1 = none */
    int      rogue_reveal_player;   /* opponent whose card is being revealed, -1 = none */
    int      rogue_reveal_card_idx; /* card index in that opponent's hand, -1 = none */
    int      duel_winner;          /* player who won the Duel trick, -1 = none */
    int      duel_target_player;   /* opponent whose card was picked, -1 = none */
    int      duel_target_card_idx; /* card index in opponent's hand, -1 = none */
    int      duel_own_card_idx;    /* winner's card to give, -1 = none */
    bool     duel_returned;        /* true = winner chose to return the card */
    int      rogue_staged_cv_idx;  /* card visual index at center, -1 = none */
    int      duel_staged_cv_idx;   /* opponent's card visual at center, -1 = none */
    int      duel_own_cv_idx;      /* winner's card visual being exchanged, -1 = none */
    bool     duel_ai_decided;      /* true = AI pre-decided, skip PICK_OWN */
} TurnFlow;

#define FLOW_TURN_TIME_LIMIT   30.0f
#define FLOW_AI_THINK_TIME     0.4f
#define FLOW_CARD_ANIM_TIME    0.25f
#define FLOW_TRICK_DISPLAY_TIME 1.0f
#define FLOW_PILE_ANIM_TIME     0.4f
#define FLOW_TRICK_COLLECT_TIME 0.3f
#define FLOW_BETWEEN_TRICKS_TIME 0.2f
#define FLOW_FOG_REVEAL_TIME    2.0f
#define FLOW_FOG_DISSOLVE_TIME  0.5f
#define FLOW_ROGUE_CHOOSE_TIME  10.0f
#define FLOW_ROGUE_REVEAL_TIME  2.0f
#define FLOW_DUEL_CHOOSE_TIME  10.0f

void flow_init(TurnFlow *flow);

void flow_update(TurnFlow *flow, GameState *gs, RenderState *rs,
                 Phase2State *p2, GameSettings *settings,
                 PlayPhaseState *pps, float dt, bool online);

#endif /* TURN_FLOW_H */
