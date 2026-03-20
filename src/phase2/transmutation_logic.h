#ifndef TRANSMUTATION_LOGIC_H
#define TRANSMUTATION_LOGIC_H

/* ============================================================
 * @deps-exports: transmute_apply(transmuter_player param),
 *                transmute_ai_apply(player_id param), transmute_is_fog(),
 *                transmute_get_transmuter(), transmute_resolve_effect(),
 *                transmute_is_effective_fog(), transmute_effect_name()
 * @deps-requires: transmutation.h (TransmuteSlot.transmuter_player,
 *                TrickTransmuteInfo.transmuter_player/resolved_effects,
 *                TEFFECT_FOG_HIDDEN, TEFFECT_MIRROR)
 * @deps-used-by: play_phase.c, info_sync.c, turn_flow.c, update.c
 * @deps-last-changed: 2026-03-20 — Mirror: added resolve/query functions
 * ============================================================ */

#include <stdbool.h>

#include "transmutation.h"
#include "core/hand.h"
#include "core/trick.h"

/* Forward declarations */
typedef struct Phase2State Phase2State;
typedef struct GameState GameState;

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
                     int hand_index, int transmutation_id,
                     int transmuter_player);

/* Keep parallel array in sync when a card is removed from hand. */
void transmute_hand_remove_at(HandTransmuteState *hts, int index, int hand_count);

/* Keep parallel array in sync when hand is sorted.
 * Accepts the same permutation that hand_sort_permutation produces. */
void transmute_hand_sort_sync(HandTransmuteState *hts, const int *perm, int count);

/* --- Query --- */
bool                    transmute_is_transmuted(const HandTransmuteState *hts, int idx);
const TransmutationDef *transmute_get_def(const HandTransmuteState *hts, int idx);
Card                    transmute_get_original(const HandTransmuteState *hts, int idx);
bool                    transmute_is_fog(const HandTransmuteState *hts, int idx);
int                     transmute_get_transmuter(const HandTransmuteState *hts, int idx);

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

/* --- Effect queries --- */

/* Returns true if the transmutation effect modifies scoring (Martyr, Gatherer, Pendulum). */
bool transmute_effect_affects_score(int transmutation_id);

/* Resolve effect handling Mirror chaining. */
TransmuteEffect transmute_resolve_effect(int transmutation_id, const Phase2State *p2);

/* Check if card is effectively fog (native Fog OR Mirror resolved to Fog). */
bool transmute_is_effective_fog(const HandTransmuteState *hts, int idx,
                                const Phase2State *p2);

/* Human-readable effect name for chat log. */
const char *transmute_effect_name(TransmuteEffect eff);

/* --- Round-scoped effect tracking --- */
void transmute_round_state_init(TransmuteRoundState *trs);
void transmute_on_trick_complete(Phase2State *p2, const Trick *trick,
                                  int winner, const TrickTransmuteInfo *tti);
void transmute_apply_round_end(Phase2State *p2,
                                int round_points[NUM_PLAYERS],
                                int total_scores[NUM_PLAYERS]);

/* --- Inter-player card swap (Duel effect) --- */
void transmute_swap_between_players(GameState *gs, Phase2State *p2,
                                     int pa, int idx_a, int pb, int idx_b);

/* --- AI --- */
void transmute_ai_apply(Hand *hand, HandTransmuteState *hts,
                        TransmuteInventory *inv, bool is_passing,
                        int player_id);

#endif /* TRANSMUTATION_LOGIC_H */
