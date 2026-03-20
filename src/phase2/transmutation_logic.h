#ifndef TRANSMUTATION_LOGIC_H
#define TRANSMUTATION_LOGIC_H

/* ============================================================
 * @deps-exports: transmute_inv_init(), transmute_inv_add(),
 *                transmute_inv_remove(), transmute_hand_init(),
 *                transmute_apply(), transmute_hand_remove_at(),
 *                transmute_hand_sort_sync(), transmute_is_transmuted(),
 *                transmute_get_def(), transmute_get_original(),
 *                transmute_card_points(), transmute_can_follow_suit(),
 *                transmute_is_always_win(), transmute_is_always_lose(),
 *                transmute_trick_get_winner(), transmute_trick_count_points(),
 *                transmute_is_valid_play(), transmute_ai_apply(),
 *                transmute_round_state_init(), transmute_on_trick_complete(),
 *                transmute_apply_round_end()
 * @deps-requires: transmutation.h, core/hand.h (Hand), core/trick.h (Trick),
 *                 phase2_state.h (Phase2State, NUM_PLAYERS), phase2_defs.h
 * @deps-used-by: transmutation_logic.c, contract_logic.c, ai.c,
 *                play_phase.c, pass_phase.c, turn_flow.c, update.c, info_sync.c, main.c
 * @deps-last-changed: 2026-03-20 — Added 3 round-scoped effect functions for Martyr
 * ============================================================ */

#include <stdbool.h>

#include "transmutation.h"
#include "core/hand.h"
#include "core/trick.h"

/* Forward declaration — full definition in phase2_state.h */
typedef struct Phase2State Phase2State;

/* --- Inventory --- */
void transmute_inv_init(TransmuteInventory *inv);
bool transmute_inv_add(TransmuteInventory *inv, int transmutation_id);
bool transmute_inv_remove(TransmuteInventory *inv, int slot_index);

/* --- Hand transmute state --- */
void transmute_hand_init(HandTransmuteState *hts);

/* Apply transmutation: replaces hand card, saves original, consumes from inv.
 * Returns false on invalid index or missing def. */
bool transmute_apply(Hand *hand, HandTransmuteState *hts,
                     TransmuteInventory *inv,
                     int hand_index, int transmutation_id);

/* Keep parallel array in sync when a card is removed from hand. */
void transmute_hand_remove_at(HandTransmuteState *hts, int index, int hand_count);

/* Keep parallel array in sync when hand is sorted.
 * Accepts the same permutation that hand_sort_permutation produces. */
void transmute_hand_sort_sync(HandTransmuteState *hts, const int *perm, int count);

/* --- Query --- */
bool                    transmute_is_transmuted(const HandTransmuteState *hts, int idx);
const TransmutationDef *transmute_get_def(const HandTransmuteState *hts, int idx);
Card                    transmute_get_original(const HandTransmuteState *hts, int idx);

/* --- Trick resolution helpers --- */
int  transmute_card_points(const HandTransmuteState *hts, int idx, Card card);
bool transmute_can_follow_suit(const HandTransmuteState *hts, int idx, Suit suit);
bool transmute_is_always_win(const HandTransmuteState *hts, int idx);
bool transmute_is_always_lose(const HandTransmuteState *hts, int idx);

/* Transmutation-aware trick winner. Falls back to trick_get_winner() when
 * no special cards are present. */
int transmute_trick_get_winner(const Trick *trick, const TrickTransmuteInfo *tti);

/* Transmutation-aware point counting for a trick. */
int transmute_trick_count_points(const Trick *trick, const TrickTransmuteInfo *tti);

/* Transmutation-aware valid play check. */
bool transmute_is_valid_play(const Trick *trick, const Hand *hand,
                             const HandTransmuteState *hts, int hand_index,
                             Card card, bool hearts_broken, bool first_trick);

/* --- Round-scoped effect tracking --- */
void transmute_round_state_init(TransmuteRoundState *trs);
void transmute_on_trick_complete(Phase2State *p2, const Trick *trick,
                                  int winner, const TrickTransmuteInfo *tti);
void transmute_apply_round_end(Phase2State *p2,
                                const int round_points[NUM_PLAYERS],
                                int total_scores[NUM_PLAYERS]);

/* --- AI --- */
void transmute_ai_apply(Hand *hand, HandTransmuteState *hts,
                        TransmuteInventory *inv, bool is_passing);

#endif /* TRANSMUTATION_LOGIC_H */
