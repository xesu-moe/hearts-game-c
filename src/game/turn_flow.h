#ifndef TURN_FLOW_H
#define TURN_FLOW_H

/* ============================================================
 * @deps-exports: enum FlowStep (includes FLOW_ROGUE_SUIT_CHOOSING),
 *                struct TurnFlow (rogue_target_player, rogue_revealed_count,
 *                rogue_staged_cv_count, rogue_staged_cv_indices[]),
 *                FLOW_ROGUE_SUIT_CHOOSE_TIME, FLOW_ROGUE_NO_CARDS_TIME,
 *                FLOW_ROGUE_REVEAL_TIME (now 3.0)
 * @deps-requires: core/game_state.h (GameState, CARDS_PER_TRICK),
 *                 core/settings.h (GameSettings), phase2/phase2_state.h,
 *                 play_phase.h (PlayPhaseState)
 * @deps-used-by: turn_flow.c, process_input.c, phase_transitions.c, main.c
 * @deps-last-changed: 2026-04-04 — Rogue redesign: added FLOW_ROGUE_SUIT_CHOOSING; replaced rogue_reveal_card_idx/rogue_staged_cv_idx with rogue_target_player/rogue_revealed_count/rogue_staged_cv_count/rogue_staged_cv_indices[]; changed FLOW_ROGUE_REVEAL_TIME from 2.0 to 3.0
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
    FLOW_ROGUE_CHOOSING,         /* pick an opponent */
    FLOW_ROGUE_SUIT_CHOOSING,    /* pick a suit from 4 windows */
    FLOW_ROGUE_WAITING,          /* waiting for server to send revealed cards */
    FLOW_ROGUE_ANIM_TO_CENTER,
    FLOW_ROGUE_REVEAL,
    FLOW_ROGUE_ANIM_BACK,
    FLOW_DUEL_PICK_OPPONENT,
    FLOW_DUEL_WAITING,           /* waiting for server to send chosen card index */
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
    Trick              saved_trick;     /* snapshot for online pile transition */
    TrickTransmuteInfo saved_tti;
    bool               has_saved_trick;
    bool     hearts_broken_at_trick_start;
    int      rogue_winner;          /* player who won the Rogue trick, -1 = none */
    int      rogue_target_player;   /* opponent selected for rogue, -1 = none */
    int      rogue_reveal_player;   /* opponent whose cards are being revealed, -1 = none */
    int      rogue_revealed_count;  /* how many cards revealed (0 = no match) */
    int      duel_winner;          /* player who won the Duel trick, -1 = none */
    int      duel_target_player;   /* opponent whose card was picked, -1 = none */
    int      duel_target_card_idx; /* card index in opponent's hand, -1 = none */
    int      duel_own_card_idx;    /* winner's card to give, -1 = none */
    bool     duel_returned;        /* true = winner chose to return the card */
    int      rogue_staged_cv_count;                 /* number of staged rogue cards */
    int      rogue_staged_cv_indices[MAX_HAND_SIZE]; /* card visual indices at center */
    int      duel_staged_cv_idx;   /* opponent's card visual at center, -1 = none */
    int      duel_own_cv_idx;      /* winner's card visual being exchanged, -1 = none */
    bool     duel_ai_decided;      /* true = AI pre-decided, skip PICK_OWN */
    bool     rogue_effect_handled; /* true = rogue already processed this trick */
    bool     duel_effect_handled;  /* true = duel already processed this trick */
} TurnFlow;

#define FLOW_TURN_TIME_LIMIT   30.0f
#define FLOW_CARD_ANIM_TIME    0.42f  /* Must exceed ANIM_TOSS_DURATION (0.35) + max jitter (0.06) */
#define FLOW_TRICK_DISPLAY_TIME 1.0f
#define FLOW_PILE_ANIM_TIME     0.4f
#define FLOW_TRICK_COLLECT_TIME 0.3f
#define FLOW_BETWEEN_TRICKS_TIME 0.2f
#define FLOW_FOG_REVEAL_TIME    2.0f
#define FLOW_FOG_DISSOLVE_TIME  0.5f
#define FLOW_ROGUE_CHOOSE_TIME       10.0f
#define FLOW_ROGUE_SUIT_CHOOSE_TIME  10.0f
#define FLOW_ROGUE_REVEAL_TIME        3.0f  /* cards face-up at center */
#define FLOW_ROGUE_NO_CARDS_TIME      2.0f  /* "no cards" message duration */
#define FLOW_DUEL_CHOOSE_TIME  10.0f

void flow_init(TurnFlow *flow);

void flow_update(TurnFlow *flow, GameState *gs, RenderState *rs,
                 Phase2State *p2, GameSettings *settings,
                 PlayPhaseState *pps, float dt);

#endif /* TURN_FLOW_H */
