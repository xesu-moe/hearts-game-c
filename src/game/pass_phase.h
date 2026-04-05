#ifndef PASS_PHASE_H
#define PASS_PHASE_H

/* ============================================================
 * @deps-exports: PassPhaseState (includes prev_draft_round field),
 *                PASS_*_TIME, advance_pass_subphase, pass_subphase_update
 * @deps-requires: core/game_state.h (GameState, PassSubphase),
 *                 core/settings.h (GameSettings), phase2/phase2_state.h
 * @deps-used-by: pass_phase.c, process_input.c, update.c, main.c
 * @deps-last-changed: 2026-03-31 — Added prev_draft_round (int) field to PassPhaseState
 * ============================================================ */

#include <stdbool.h>

#include "core/game_state.h"
#include "phase2/phase2_state.h"

/* Forward declarations */
typedef struct RenderState RenderState;
typedef struct GameSettings GameSettings;

#define PASS_DEALER_TIME         30.0f
#define PASS_CONTRACT_TIME       15.0f  /* must match DRAFT_TIMER_SECONDS */
#define PASS_CARD_PASS_TIME      20.0f
#define PASS_REVEAL_DURATION     2.0f   /* show received cards face-up in staging */
#define PASS_AI_DEALER_DISPLAY   1.2f   /* brief delay for AI dealer choice */
#define PASS_DEALER_ANNOUNCE     1.0f   /* show dealer choice message before contracts */

/* Valid dealer amounts */
static const int DEALER_AMOUNTS[] = {0, 2, 3, 4};
#define DEALER_AMOUNT_COUNT 4

typedef struct PassPhaseState {
    PassSubphase subphase;
    float        timer;
    bool         dealer_ui_active;
    bool         ai_dealer_pending;
    bool         dealer_announced;  /* true = showing announcement, waiting */
    float        dealer_announce_timer;
    /* Dealer selection state */
    int          dealer_dir;   /* PassDirection: 0=left, 1=right, 2=across */
    int          dealer_amt;   /* 0, 2, 3, or 4 */
    /* Pass animation state */
    bool         pass_anim;         /* true while pass animation is running */
    Card         received_cards[MAX_PASS_CARD_COUNT];
    int          received_count;
    bool         draft_pick_pending;  /* online: pick sent, awaiting server confirm */
    int          draft_pick_round;   /* round number when pending pick was sent */
    bool         draft_click_consumed; /* true after contract pick until mouse released */
    int          prev_draft_round;    /* track draft round changes to reset timer */
    bool         pass_auto_sent;      /* online: pass timeout commands already pushed */
    /* Async toss tracking */
    bool         toss_started[NUM_PLAYERS]; /* per-seat: toss anim fired */
    int          toss_count;                /* how many seats have tossed */
    bool         async_toss;                /* true = in incremental toss mode */
} PassPhaseState;

/* Determine dealer: player with highest prev_round_points.
 * Returns -1 if round_number <= 1. Breaks ties by total score, then lowest id. */
int dealer_determine_player(const int prev_round_points[NUM_PLAYERS],
                            const GameState *gs);

void setup_dealer_ui(PassPhaseState *pps, RenderState *rs, int dealer_player_id);

/* Show dealer choice announcement and push to chat log. */
void dealer_announce(PassPhaseState *pps, RenderState *rs);

void advance_pass_subphase(PassPhaseState *pps, GameState *gs,
                           RenderState *rs, Phase2State *p2,
                           PassSubphase next);

void auto_select_human_pass(GameState *gs, RenderState *rs);



/* Check if all toss animations have completed. */
bool pass_toss_animations_done(const RenderState *rs);

/* Start toss animation using local data + card-backs for opponents.
 * received_cards are the cards the human will receive (computed by
 * diffing pre-pass hand vs post-pass hand from server). */
void pass_start_toss_anim_batched(PassPhaseState *pps, GameState *gs,
                                  RenderState *rs,
                                  const Card *received_cards, int received_count);

/* Async toss: start toss animation for a single player.
 * For seat 0 (human): uses known card identities from pass_selections.
 * For opponents: uses face-down dummy cards. */
void pass_start_single_toss(PassPhaseState *pps, GameState *gs,
                            RenderState *rs, int seat);

/* Assign received card identities to already-staged face-down cards destined
 * for the human player. Called when the PASSING->PLAYING state arrives. */
void pass_assign_received_cards(PassPhaseState *pps, RenderState *rs,
                                const Card *received, int count);

/* Animate cards into final positions without calling
 * game_state_execute_pass (server already did it). */
void pass_start_receive_anim(PassPhaseState *pps, GameState *gs,
                             RenderState *rs,
                             const GameSettings *settings);

/* Check if all receive animations have completed. */
bool pass_receive_animations_done(const RenderState *rs);

void pass_subphase_update(PassPhaseState *pps, GameState *gs,
                          RenderState *rs, Phase2State *p2,
                          const GameSettings *settings, float dt);

void setup_draft_ui(RenderState *rs, Phase2State *p2);

/* Sync pass phase UI from server state.
 * Called after state_recv_apply to populate render state for the current
 * server-driven subphase. */
void pass_sync_ui(PassPhaseState *pps, GameState *gs,
                  RenderState *rs, Phase2State *p2);

/* Advance draft to next round or finalize. Called after all players pick. */
void draft_finish_round(PassPhaseState *pps, GameState *gs,
                        RenderState *rs, Phase2State *p2);

float pass_subphase_time_limit(PassSubphase sub);

#endif /* PASS_PHASE_H */
