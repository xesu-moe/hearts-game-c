/* ============================================================
 * @deps-implements: transmutation_logic.h
 * @deps-requires: transmutation_logic.h, transmutation.h (TransmuteRoundState.rogue_revealed_card, duel_revealed_card),
 *                 phase2_defs.h, phase2_state.h (shield_tricks_remaining[], curse_force_hearts[]),
 *                 core/hand.h (hand_contains, hand_has_suit), core/trick.h, core/card.h (SUIT_HEARTS, Card),
 *                 core/game_state.h (GameState, hearts_broken), string.h, stdio.h, stdlib.h
 * @deps-last-changed: 2026-04-04 — Uses TransmuteRoundState rogue/duel revealed card fields for Rogue/Duel mechanics
 * ============================================================ */

#include "transmutation_logic.h"

#include <string.h>

#include <stdio.h>
#include <stdlib.h>

#include "core/game_state.h"
#include "phase2_defs.h"
#include "phase2_state.h"

/* ----------------------------------------------------------------
 * Inventory
 * ---------------------------------------------------------------- */

void transmute_inv_init(TransmuteInventory *inv)
{
    for (int i = 0; i < MAX_TRANSMUTE_INVENTORY; i++) {
        inv->items[i] = -1;
    }
    inv->count = 0;
}

bool transmute_inv_add(TransmuteInventory *inv, int transmutation_id)
{
    if (inv->count >= MAX_TRANSMUTE_INVENTORY) return false;
    inv->items[inv->count] = transmutation_id;
    inv->count++;
    return true;
}

bool transmute_inv_remove(TransmuteInventory *inv, int slot_index)
{
    if (slot_index < 0 || slot_index >= inv->count) return false;
    inv->count--;
    for (int i = slot_index; i < inv->count; i++) {
        inv->items[i] = inv->items[i + 1];
    }
    inv->items[inv->count] = -1;
    return true;
}

/* ----------------------------------------------------------------
 * Hand transmute state
 * ---------------------------------------------------------------- */

void transmute_hand_init(HandTransmuteState *hts)
{
    for (int i = 0; i < MAX_HAND_SIZE; i++) {
        hts->slots[i].transmutation_id = -1;
        hts->slots[i].original_card = CARD_NONE;
        hts->slots[i].transmuter_player = -1;
        hts->slots[i].fogged = false;
        hts->slots[i].fog_transmuter = -1;
    }
}

bool transmute_apply(Hand *hand, HandTransmuteState *hts,
                     TransmuteInventory *inv,
                     int hand_index, int transmutation_id,
                     int transmuter_player)
{
    if (hand_index < 0 || hand_index >= hand->count) return false;

    const TransmutationDef *td = phase2_get_transmutation(transmutation_id);
    if (!td) return false;

    /* Restricted cards */
    Card target = hand->cards[hand_index];
    if (target.suit == SUIT_CLUBS && target.rank == RANK_2)
        return false;  /* 2 of Clubs: never transmutable */
    if (target.suit == SUIT_SPADES && target.rank == RANK_Q &&
        td->effect != TEFFECT_FOG_HIDDEN)
        return false;  /* Queen of Spades: fog only */

    bool is_fog = (td->effect == TEFFECT_FOG_HIDDEN);
    bool already_transmuted = (hts->slots[hand_index].transmutation_id >= 0);

    /* Stacking guards */
    if (already_transmuted) {
        if (!is_fog) return false;                    /* non-fog on transmuted: blocked */
        if (hts->slots[hand_index].fogged) return false; /* double-fog: blocked */
    }

    /* Find and consume from inventory */
    int inv_slot = -1;
    for (int i = 0; i < inv->count; i++) {
        if (inv->items[i] == transmutation_id) {
            inv_slot = i;
            break;
        }
    }
    if (inv_slot < 0) return false;

    if (already_transmuted && is_fog) {
        /* Fog stacking: overlay fog on existing transmutation.
         * Preserve transmutation_id, original_card, transmuter_player. */
        hts->slots[hand_index].fogged = true;
        hts->slots[hand_index].fog_transmuter = transmuter_player;
        transmute_inv_remove(inv, inv_slot);
        return true;
    }

    /* Normal apply (no existing transmutation) */
    hts->slots[hand_index].original_card = hand->cards[hand_index];
    hts->slots[hand_index].transmutation_id = transmutation_id;
    hts->slots[hand_index].transmuter_player = transmuter_player;

    if (is_fog) {
        /* Pure fog on non-transmuted card: mark as fogged */
        hts->slots[hand_index].fogged = true;
        hts->slots[hand_index].fog_transmuter = transmuter_player;
    } else {
        /* Replace hand card with transmuted version */
        hand->cards[hand_index].suit = td->result_suit;
        hand->cards[hand_index].rank = td->result_rank;
    }

    /* Consume from inventory */
    transmute_inv_remove(inv, inv_slot);

    return true;
}

void transmute_hand_remove_at(HandTransmuteState *hts, int index, int hand_count)
{
    if (index < 0 || hand_count < 0 || hand_count >= MAX_HAND_SIZE) return;
    /* Shift slots left to stay in sync with hand (hand_count is AFTER removal) */
    for (int i = index; i < hand_count; i++) {
        hts->slots[i] = hts->slots[i + 1];
    }
    /* Clear the now-unused last slot */
    hts->slots[hand_count].transmutation_id = -1;
    hts->slots[hand_count].original_card = CARD_NONE;
    hts->slots[hand_count].transmuter_player = -1;
    hts->slots[hand_count].fogged = false;
    hts->slots[hand_count].fog_transmuter = -1;
}

void transmute_hand_sort_sync(HandTransmuteState *hts, const int *perm, int count)
{
    TransmuteSlot tmp[MAX_HAND_SIZE];
    for (int i = 0; i < count; i++) {
        tmp[i] = hts->slots[perm[i]];
    }
    for (int i = 0; i < count; i++) {
        hts->slots[i] = tmp[i];
    }
}

/* ----------------------------------------------------------------
 * Query
 * ---------------------------------------------------------------- */

bool transmute_is_transmuted(const HandTransmuteState *hts, int idx)
{
    if (idx < 0 || idx >= MAX_HAND_SIZE) return false;
    return hts->slots[idx].transmutation_id >= 0;
}

const TransmutationDef *transmute_get_def(const HandTransmuteState *hts, int idx)
{
    if (!transmute_is_transmuted(hts, idx)) return NULL;
    return phase2_get_transmutation(hts->slots[idx].transmutation_id);
}

Card transmute_get_original(const HandTransmuteState *hts, int idx)
{
    if (!transmute_is_transmuted(hts, idx)) return CARD_NONE;
    return hts->slots[idx].original_card;
}

bool transmute_is_fog(const HandTransmuteState *hts, int idx)
{
    const TransmutationDef *td = transmute_get_def(hts, idx);
    return td && td->effect == TEFFECT_FOG_HIDDEN;
}

int transmute_get_transmuter(const HandTransmuteState *hts, int idx)
{
    if (idx < 0 || idx >= MAX_HAND_SIZE) return -1;
    return hts->slots[idx].transmuter_player;
}

int transmute_get_fog_transmuter(const HandTransmuteState *hts, int idx)
{
    if (idx < 0 || idx >= MAX_HAND_SIZE) return -1;
    if (!hts->slots[idx].fogged) return -1;
    return hts->slots[idx].fog_transmuter;
}

/* ----------------------------------------------------------------
 * Trick resolution helpers
 * ---------------------------------------------------------------- */

int transmute_card_points(const HandTransmuteState *hts, int idx, Card card)
{
    const TransmutationDef *td = transmute_get_def(hts, idx);
    if (td && td->custom_points >= 0) return td->custom_points;
    return card_points(card);
}

bool transmute_can_follow_suit(const HandTransmuteState *hts, int idx, Suit suit)
{
    const TransmutationDef *td = transmute_get_def(hts, idx);
    if (!td) return false; /* not transmuted, use normal rules */
    if (td->suit_mask == SUIT_MASK_NONE) return false; /* no mask override */
    return (td->suit_mask & (1 << suit)) != 0;
}

bool transmute_is_always_win(const HandTransmuteState *hts, int idx)
{
    const TransmutationDef *td = transmute_get_def(hts, idx);
    return td && td->special == TRANSMUTE_ALWAYS_WIN;
}

bool transmute_is_always_lose(const HandTransmuteState *hts, int idx)
{
    const TransmutationDef *td = transmute_get_def(hts, idx);
    return td && td->special == TRANSMUTE_ALWAYS_LOSE;
}

int transmute_trick_get_winner(const Trick *trick, const TrickTransmuteInfo *tti,
                               const Phase2State *p2)
{
    if (!trick_is_complete(trick)) return -1;

    /* Check for ALWAYS_WIN / ALWAYS_LOSE / Roulette / Binding / Crown / Joker */
    bool has_special = false;
    if (tti) {
        for (int i = 0; i < CARDS_PER_TRICK; i++) {
            if (tti->transmutation_ids[i] >= 0) {
                const TransmutationDef *td =
                    phase2_get_transmutation(tti->transmutation_ids[i]);
                if (td && td->special != TRANSMUTE_NORMAL) {
                    has_special = true;
                    break;
                }
            }
            if (tti->resolved_effects[i] == TEFFECT_RANDOM_TRICK_WINNER ||
                tti->resolved_effects[i] == TEFFECT_CROWN_HIGHEST_RANK ||
                tti->resolved_effects[i] == TEFFECT_JOKER_LEAD_WIN) {
                has_special = true;
                break;
            }
        }
    }
    /* Binding: check if any player in the trick has auto-win active */
    if (!has_special && p2) {
        for (int i = 0; i < CARDS_PER_TRICK; i++) {
            int pid = trick->player_ids[i];
            if (pid >= 0 && pid < NUM_PLAYERS && p2->binding_auto_win[pid]) {
                has_special = true;
                break;
            }
        }
    }

    if (!has_special) {
        return trick_get_winner(trick);
    }

    /* If only Binding triggered has_special (no tti), resolve Binding then fallback */
    if (!tti) {
        if (p2) {
            for (int i = 0; i < CARDS_PER_TRICK; i++) {
                int pid = trick->player_ids[i];
                if (pid >= 0 && pid < NUM_PLAYERS && p2->binding_auto_win[pid])
                    return pid;
            }
        }
        return trick_get_winner(trick);
    }

    /* Leading Joker: absolute priority — beats everything including ALWAYS_WIN.
     * Only the lead card (index 0) can trigger this. */
    if (tti->resolved_effects[0] == TEFFECT_JOKER_LEAD_WIN) {
        return trick->player_ids[0];
    }

    /* Find the ALWAYS_WIN card (last one wins if multiple) — highest priority */
    int always_win_idx = -1;
    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        if (tti->transmutation_ids[i] >= 0) {
            const TransmutationDef *td =
                phase2_get_transmutation(tti->transmutation_ids[i]);
            if (td && td->special == TRANSMUTE_ALWAYS_WIN) {
                always_win_idx = i;
            }
        }
    }

    if (always_win_idx >= 0) {
        return trick->player_ids[always_win_idx];
    }

    /* Binding: auto-win (weaker than jokers, stronger than Roulette).
     * Non-leading Joker overrides Binding — that player still loses. */
    if (p2) {
        for (int i = 0; i < CARDS_PER_TRICK; i++) {
            int pid = trick->player_ids[i];
            if (pid >= 0 && pid < NUM_PLAYERS && p2->binding_auto_win[pid]) {
                /* Skip if this player's card is a non-leading Joker */
                if (i > 0 && tti->resolved_effects[i] == TEFFECT_JOKER_LEAD_WIN)
                    continue;
                return pid;
            }
        }
    }

    /* Check for Roulette: random trick winner.
     * Excludes ALWAYS_LOSE cards and non-leading Jokers from candidates. */
    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        if (tti->resolved_effects[i] == TEFFECT_RANDOM_TRICK_WINNER) {
            int candidates[CARDS_PER_TRICK];
            int num_candidates = 0;
            for (int j = 0; j < CARDS_PER_TRICK; j++) {
                if (tti->transmutation_ids[j] >= 0) {
                    const TransmutationDef *td =
                        phase2_get_transmutation(tti->transmutation_ids[j]);
                    if (td && td->special == TRANSMUTE_ALWAYS_LOSE) continue;
                }
                if (j > 0 && tti->resolved_effects[j] == TEFFECT_JOKER_LEAD_WIN)
                    continue;
                candidates[num_candidates++] = j;
            }
            if (num_candidates > 0) {
                int pick = (rand() % num_candidates);
                return trick->player_ids[candidates[pick]];
            }
            break; /* All excluded — fall through to normal */
        }
    }

    /* Crown: highest rank across all suits wins (first played breaks ties).
     * Excludes ALWAYS_LOSE cards and non-leading Jokers. */
    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        if (tti->resolved_effects[i] == TEFFECT_CROWN_HIGHEST_RANK) {
            int best_idx = -1;
            Rank best_rank = RANK_2;
            for (int j = 0; j < CARDS_PER_TRICK; j++) {
                if (tti->transmutation_ids[j] >= 0) {
                    const TransmutationDef *td =
                        phase2_get_transmutation(tti->transmutation_ids[j]);
                    if (td && td->special == TRANSMUTE_ALWAYS_LOSE) continue;
                }
                /* Non-leading Joker: excluded (always loses) */
                if (j > 0 && tti->resolved_effects[j] == TEFFECT_JOKER_LEAD_WIN)
                    continue;
                if (best_idx < 0 || trick->cards[j].rank > best_rank) {
                    best_idx = j;
                    best_rank = trick->cards[j].rank;
                }
            }
            if (best_idx >= 0) return trick->player_ids[best_idx];
            break;
        }
    }

    /* No ALWAYS_WIN — fall back to normal, but exclude ALWAYS_LOSE cards
     * and non-leading Jokers (always lose when not leading). */
    int best_idx = -1;
    Rank best_rank = RANK_2;
    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        /* Skip ALWAYS_LOSE cards */
        if (tti->transmutation_ids[i] >= 0) {
            const TransmutationDef *td =
                phase2_get_transmutation(tti->transmutation_ids[i]);
            if (td && td->special == TRANSMUTE_ALWAYS_LOSE) continue;
        }
        /* Non-leading Joker: excluded (always loses) */
        if (i > 0 && tti->resolved_effects[i] == TEFFECT_JOKER_LEAD_WIN)
            continue;

        if (trick->cards[i].suit == trick->lead_suit) {
            if (best_idx < 0 || trick->cards[i].rank > best_rank) {
                best_idx = i;
                best_rank = trick->cards[i].rank;
            }
        }
    }

    if (best_idx >= 0) {
        return trick->player_ids[best_idx];
    }

    /* All lead-suit cards are ALWAYS_LOSE or non-leading Jokers.
     * Cannot happen for Joker alone: index 0 (leading) is always caught
     * at the leading-Joker check above. Fallback to standard resolution. */
    return trick_get_winner(trick);
}

int transmute_trick_count_points(const Trick *trick, const TrickTransmuteInfo *tti,
                                 const Phase2State *p2)
{
    if (!tti) return trick_count_points(trick);

    /* Check for trick-wide effects */
    bool has_trap = false;
    bool has_inversion = false;
    for (int i = 0; i < trick->num_played; i++) {
        if (tti->resolved_effects[i] == TEFFECT_TRAP_DOUBLE_WITH_QOS)
            has_trap = true;
        if (tti->resolved_effects[i] == TEFFECT_INVERSION_NEGATE_POINTS)
            has_inversion = true;
    }

    int points = 0;
    for (int i = 0; i < trick->num_played; i++) {
        int card_pts;
        if (tti->transmutation_ids[i] >= 0) {
            const TransmutationDef *td =
                phase2_get_transmutation(tti->transmutation_ids[i]);
            if (td && td->custom_points >= 0) {
                card_pts = td->custom_points;
            } else if (td && td->effect == TEFFECT_MIRROR && p2) {
                /* Mirror inherits point value from the source card */
                card_pts = card_points(p2->last_played_transmuted_card);
            } else {
                card_pts = card_points(trick->cards[i]);
            }
        } else {
            card_pts = card_points(trick->cards[i]);
        }

        /* Trap: double points for any Queen of Spades */
        if (has_trap &&
            trick->cards[i].suit == SUIT_SPADES &&
            trick->cards[i].rank == RANK_Q) {
            card_pts *= 2;
        }

        /* Inversion: negate any card with non-zero points, including
         * custom_points from transmutations (applied after Trap) */
        if (has_inversion && card_pts != 0) {
            card_pts = -card_pts;
        }

        points += card_pts;
    }
    return points;
}

bool transmute_is_valid_play(const Trick *trick, const Hand *hand,
                             const HandTransmuteState *hts, int hand_index,
                             Card card, bool hearts_broken, bool first_trick)
{
    /* Check if this card is transmuted with a suit_mask */
    if (hts && transmute_is_transmuted(hts, hand_index)) {
        const TransmutationDef *td = transmute_get_def(hts, hand_index);
        if (td && td->suit_mask != SUIT_MASK_NONE) {
            /* Must have the card in hand */
            if (hand_index < 0 || hand_index >= hand->count) return false;
            if (!card_equals(hand->cards[hand_index], card)) return false;

            bool leading = (trick->num_played == 0);

            if (first_trick && leading) {
                /* Must play 2 of clubs on first trick lead */
                Card two_of_clubs = {SUIT_CLUBS, RANK_2};
                return card_equals(card, two_of_clubs);
            }

            /* First trick following: can't play penalty cards unless forced */
            if (first_trick && !leading) {
                if (hand_has_suit(hand, trick->lead_suit)) {
                    return (td->suit_mask & (1 << trick->lead_suit)) != 0;
                }
                /* Can't follow suit: check if this is a penalty card */
                bool is_penalty = (td->custom_points > 0) ||
                                  (td->custom_points < 0 &&
                                   card_points(card) > 0);
                if (is_penalty) {
                    /* Only allow if all cards in hand are penalty */
                    for (int i = 0; i < hand->count; i++) {
                        if (i == hand_index) continue;
                        if (card_points(hand->cards[i]) == 0) {
                            /* Check transmuted cards too */
                            if (transmute_is_transmuted(hts, i)) {
                                const TransmutationDef *otd =
                                    transmute_get_def(hts, i);
                                if (otd && otd->custom_points > 0)
                                    continue;
                            }
                            return false;
                        }
                    }
                }
                return true;
            }

            if (td->suit_mask == SUIT_MASK_ALL) {
                /* Suitless: can be played anytime */
                return true;
            }

            if (!leading) {
                /* Following: card can follow if its mask includes lead suit */
                if (td->suit_mask & (1 << trick->lead_suit)) {
                    return true;
                }
                /* If player has no cards of lead suit (considering masks), ok */
                bool can_follow = false;
                for (int i = 0; i < hand->count; i++) {
                    if (i == hand_index) continue;
                    if (hand->cards[i].suit == trick->lead_suit) {
                        can_follow = true;
                        break;
                    }
                    /* Check if another transmuted card can follow */
                    if (transmute_is_transmuted(hts, i)) {
                        const TransmutationDef *otd = transmute_get_def(hts, i);
                        if (otd && (otd->suit_mask & (1 << trick->lead_suit))) {
                            can_follow = true;
                            break;
                        }
                    }
                }
                return !can_follow;
            }

            /* Leading with a masked card */
            if (!hearts_broken) {
                /* Can't lead hearts-only mask unless all cards are hearts */
                bool only_hearts_mask = (td->suit_mask == SUIT_MASK_HEARTS);
                if (only_hearts_mask) {
                    /* If any other card in hand is non-hearts, can't lead this */
                    for (int i = 0; i < hand->count; i++) {
                        if (i == hand_index) continue;
                        if (hand->cards[i].suit != SUIT_HEARTS) {
                            /* Check if this other card is also hearts-only mask */
                            if (transmute_is_transmuted(hts, i)) {
                                const TransmutationDef *otd = transmute_get_def(hts, i);
                                if (otd && otd->suit_mask != SUIT_MASK_NONE &&
                                    !(otd->suit_mask & ~SUIT_MASK_HEARTS)) {
                                    continue; /* Also hearts-only */
                                }
                            }
                            return false; /* Has a non-hearts alternative */
                        }
                    }
                }
            }
            return true;
        }
    }

    /* No transmutation override — use standard rules */
    return trick_is_valid_play(trick, hand, card, hearts_broken, first_trick);
}

bool transmute_curse_is_valid_lead(const Hand *hand, Card card)
{
    if (!hand_contains(hand, card)) return false;
    /* If player has any hearts, must play a heart */
    if (hand_has_suit(hand, SUIT_HEARTS)) {
        return card.suit == SUIT_HEARTS;
    }
    /* No hearts: any card is valid */
    return true;
}

void transmute_curse_consume(Phase2State *p2, GameState *gs, int player_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    if (!p2->curse_force_hearts[player_id]) return;
    p2->curse_force_hearts[player_id] = false;
    gs->hearts_broken = true;
    fprintf(stderr,
             "TRANSMUTE: Curse consumed for player %d, hearts broken\n",
             player_id);
}

bool transmute_anchor_is_valid_lead(const Hand *hand, Card card, Suit forced_suit)
{
    if (hand_has_suit(hand, forced_suit)) {
        return card.suit == forced_suit;
    }
    /* No cards of forced suit — effect expires, any card ok */
    return true;
}

void transmute_anchor_consume(Phase2State *p2, int player_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS) return;
    p2->anchor_force_suit[player_id] = -1;
    fprintf(stderr, "TRANSMUTE: Anchor consumed for player %d\n", player_id);
}

/* ----------------------------------------------------------------
 * Effect queries
 * ---------------------------------------------------------------- */

bool transmute_effect_affects_score(int transmutation_id)
{
    if (transmutation_id < 0) return false;
    const TransmutationDef *td = phase2_get_transmutation(transmutation_id);
    if (!td) return false;
    return td->effect == TEFFECT_WOTT_DUPLICATE_ROUND_POINTS ||
           td->effect == TEFFECT_WOTT_REDUCE_SCORE_3 ||
           td->effect == TEFFECT_WOTT_REDUCE_SCORE_1 ||
           td->effect == TEFFECT_TRAP_DOUBLE_WITH_QOS ||
           td->effect == TEFFECT_WOTT_SHIELD_NEXT_TRICK ||
           td->effect == TEFFECT_PARASITE_REDIRECT_POINTS ||
           td->effect == TEFFECT_BOUNTY_REDIRECT_QOS ||
           td->effect == TEFFECT_INVERSION_NEGATE_POINTS ||
           td->effect == TEFFECT_MIRROR; /* conservative: may resolve to scoring effect */
}

TransmuteEffect transmute_resolve_effect(int transmutation_id, const Phase2State *p2)
{
    if (transmutation_id < 0) return TEFFECT_NONE;
    const TransmutationDef *td = phase2_get_transmutation(transmutation_id);
    if (!td) return TEFFECT_NONE;
    if (td->effect != TEFFECT_MIRROR) return td->effect;
    /* Mirror: return last globally-played resolved effect (implicit chain).
     * Joker effect excluded — positional win/lose makes no sense on Mirror. */
    if (p2 && p2->last_played_resolved_effect != TEFFECT_JOKER_LEAD_WIN)
        return p2->last_played_resolved_effect;
    return TEFFECT_NONE;
}

bool transmute_is_effective_fog(const HandTransmuteState *hts, int idx,
                                const Phase2State *p2)
{
    if (idx < 0 || idx >= MAX_HAND_SIZE) return false;
    /* Fog overlay (stacked or pure) */
    if (hts->slots[idx].fogged) return true;
    /* Mirror resolved to fog */
    if (!transmute_is_transmuted(hts, idx)) return false;
    const TransmutationDef *td = transmute_get_def(hts, idx);
    if (!td) return false;
    if (td->effect == TEFFECT_MIRROR && p2) {
        return p2->last_played_resolved_effect == TEFFECT_FOG_HIDDEN;
    }
    return false;
}

const char *transmute_effect_name(TransmuteEffect eff)
{
    switch (eff) {
    case TEFFECT_NONE:                       return "None";
    case TEFFECT_WOTT_DUPLICATE_ROUND_POINTS: return "Martyr";
    case TEFFECT_WOTT_REDUCE_SCORE_3:        return "Gatherer";
    case TEFFECT_WOTT_REVEAL_OPPONENT_CARD:  return "Rogue";
    case TEFFECT_WOTT_REDUCE_SCORE_1:        return "Pendulum";
    case TEFFECT_WOTT_SWAP_CARD:             return "Duel";
    case TEFFECT_FOG_HIDDEN:                 return "Fog";
    case TEFFECT_MIRROR:                     return "Mirror";
    case TEFFECT_RANDOM_TRICK_WINNER:        return "Roulette";
    case TEFFECT_TRAP_DOUBLE_WITH_QOS:       return "Trap";
    case TEFFECT_WOTT_SHIELD_NEXT_TRICK:     return "Shield";
    case TEFFECT_WOTT_FORCE_LEAD_HEARTS:     return "Curse";
    case TEFFECT_ANCHOR_FORCE_LEAD_SUIT:     return "Anchor";
    case TEFFECT_BINDING_AUTO_WIN_NEXT:      return "Binding";
    case TEFFECT_CROWN_HIGHEST_RANK:         return "Crown";
    case TEFFECT_PARASITE_REDIRECT_POINTS:   return "Parasite";
    case TEFFECT_BOUNTY_REDIRECT_QOS:        return "Bounty";
    case TEFFECT_INVERSION_NEGATE_POINTS:    return "Inversion";
    case TEFFECT_JOKER_LEAD_WIN:             return "Joker";
    default:                                 return "Unknown";
    }
}

/* ----------------------------------------------------------------
 * Round-scoped effect tracking
 * ---------------------------------------------------------------- */

void transmute_round_state_init(TransmuteRoundState *trs)
{
    memset(trs, 0, sizeof(*trs));
    trs->rogue_pending_winner = -1;
    trs->duel_pending_winner = -1;
    trs->rogue_chosen_suit = -1;
    trs->rogue_chosen_target = -1;
    trs->rogue_revealed_count = -1;
    trs->duel_chosen_card_idx = -1;
    trs->duel_chosen_target = -1;
    trs->duel_revealed_card = (Card){-1, -1};
}

void transmute_on_trick_complete(Phase2State *p2, const Trick *trick,
                                  int winner, const TrickTransmuteInfo *tti)
{
    /* trick used by Anchor to read lead_suit.
     * NOTE: Parasite (TEFFECT_PARASITE_REDIRECT_POINTS) is handled
     * directly in turn_flow.c FLOW_TRICK_COLLECTING, not here. */
    if (!p2 || !tti || winner < 0 || winner >= NUM_PLAYERS) return;

    /* Detect Inversion in this trick — flips Gatherer/Pendulum reductions */
    bool has_inversion = false;
    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        if (tti->resolved_effects[i] == TEFFECT_INVERSION_NEGATE_POINTS) {
            has_inversion = true;
            break;
        }
    }

    for (int i = 0; i < CARDS_PER_TRICK; i++) {
        if (tti->transmutation_ids[i] < 0) continue;
        TransmuteEffect eff = tti->resolved_effects[i];

        if (eff == TEFFECT_WOTT_DUPLICATE_ROUND_POINTS) {
            p2->round.transmute_round.martyr_flags[winner] += 1;
            fprintf(stderr, "TRANSMUTE: Martyr effect flagged for player %d\n",
                     winner);
        } else if (eff == TEFFECT_WOTT_REDUCE_SCORE_3) {
            int delta = has_inversion ? -3 : 3;
            p2->round.transmute_round.gatherer_reduction[winner] += delta;
            fprintf(stderr,
                     "TRANSMUTE: Gatherer %+d for player %d (total %d)%s\n",
                     delta, winner,
                     p2->round.transmute_round.gatherer_reduction[winner],
                     has_inversion ? " [Inverted]" : "");
        } else if (eff == TEFFECT_WOTT_REVEAL_OPPONENT_CARD) {
            p2->round.transmute_round.rogue_pending_winner = winner;
            fprintf(stderr,
                     "TRANSMUTE: Rogue reveal flagged for player %d\n", winner);
        } else if (eff == TEFFECT_WOTT_REDUCE_SCORE_1) {
            int delta = has_inversion ? -1 : 1;
            p2->round.transmute_round.gatherer_reduction[winner] += delta;
            fprintf(stderr,
                     "TRANSMUTE: Pendulum %+d for player %d (total %d)%s\n",
                     delta, winner,
                     p2->round.transmute_round.gatherer_reduction[winner],
                     has_inversion ? " [Inverted]" : "");
        } else if (eff == TEFFECT_WOTT_SWAP_CARD) {
            p2->round.transmute_round.duel_pending_winner = winner;
            fprintf(stderr,
                     "TRANSMUTE: Duel swap flagged for player %d\n", winner);
        } else if (eff == TEFFECT_WOTT_SHIELD_NEXT_TRICK) {
            p2->shield_tricks_remaining[winner] = 3;
            fprintf(stderr,
                     "TRANSMUTE: Shield activated for player %d (3 tricks)\n",
                     winner);
        } else if (eff == TEFFECT_WOTT_FORCE_LEAD_HEARTS) {
            p2->curse_force_hearts[winner] = true;
            fprintf(stderr,
                     "TRANSMUTE: Curse activated for player %d (must lead hearts)\n",
                     winner);
        } else if (eff == TEFFECT_ANCHOR_FORCE_LEAD_SUIT) {
            p2->anchor_force_suit[winner] = trick->lead_suit;
            fprintf(stderr,
                     "TRANSMUTE: Anchor set for player %d (suit %d)\n",
                     winner, trick->lead_suit);
        } else if (eff == TEFFECT_BINDING_AUTO_WIN_NEXT) {
            p2->binding_auto_win[winner] = 1;
            fprintf(stderr,
                     "TRANSMUTE: Binding activated for player %d\n", winner);
        }
    }
}

void transmute_apply_round_end(Phase2State *p2,
                                int round_points[NUM_PLAYERS],
                                int total_scores[NUM_PLAYERS])
{
    if (!p2) return;

    /* Gatherer/Pendulum: reduce scores (floor at 0 for reductions).
     * Negative values (from Inversion flipping) add points instead.
     * Applied BEFORE Martyr so the multiplier applies to the adjusted value. */
    for (int i = 0; i < NUM_PLAYERS; i++) {
        int red = p2->round.transmute_round.gatherer_reduction[i];
        if (red > 0) {
            round_points[i] -= red;
            if (round_points[i] < 0) round_points[i] = 0;
            total_scores[i] -= red;
            if (total_scores[i] < 0) total_scores[i] = 0;
            fprintf(stderr,
                     "TRANSMUTE: Gatherer reduces player %d by %d, \n"
                     "round = %d, total = %d",
                     i, red, round_points[i], total_scores[i]);
        } else if (red < 0) {
            /* Inversion flipped reductions to additions */
            round_points[i] -= red; /* -= negative = addition */
            total_scores[i] -= red;
            fprintf(stderr,
                     "TRANSMUTE: Inverted Gatherer adds %d to player %d, \n"
                     "round = %d, total = %d",
                     -red, i, round_points[i], total_scores[i]);
        }
    }

    /* Martyr: multiply round points by 2^count (1 martyr = x2, 2 = x4, 3 = x8).
     * Applied AFTER Gatherer so the multiplier applies to the adjusted value. */
    for (int i = 0; i < NUM_PLAYERS; i++) {
        int count = p2->round.transmute_round.martyr_flags[i];
        if (count > 0) {
            int multiplier = 1 << count; /* 2^count */
            int original = round_points[i];
            int added = original * (multiplier - 1);
            round_points[i] += added;
            total_scores[i] += added;
            fprintf(stderr,
                     "TRANSMUTE: Martyr x%d player %d round_points (%d -> %d), \n"
                     "new total = %d",
                     multiplier, i, original, round_points[i], total_scores[i]);
        }
    }
}

/* ----------------------------------------------------------------
 * Inter-player card swap (Duel effect)
 * ---------------------------------------------------------------- */

void transmute_swap_between_players(GameState *gs, Phase2State *p2,
                                     int pa, int idx_a, int pb, int idx_b)
{
    if (pa < 0 || pa >= NUM_PLAYERS || pb < 0 || pb >= NUM_PLAYERS) return;
    if (idx_a < 0 || idx_a >= gs->players[pa].hand.count) return;
    if (idx_b < 0 || idx_b >= gs->players[pb].hand.count) return;

    /* Swap card values */
    Card tmp = gs->players[pa].hand.cards[idx_a];
    gs->players[pa].hand.cards[idx_a] = gs->players[pb].hand.cards[idx_b];
    gs->players[pb].hand.cards[idx_b] = tmp;

    /* Swap transmutation metadata */
    TransmuteSlot tmp_slot = p2->players[pa].hand_transmutes.slots[idx_a];
    p2->players[pa].hand_transmutes.slots[idx_a] =
        p2->players[pb].hand_transmutes.slots[idx_b];
    p2->players[pb].hand_transmutes.slots[idx_b] = tmp_slot;
}

/* ----------------------------------------------------------------
 * AI
 * ---------------------------------------------------------------- */

void transmute_ai_apply(Hand *hand, HandTransmuteState *hts,
                        TransmuteInventory *inv, bool is_passing,
                        int player_id)
{
    if (inv->count <= 0) return;

    /* Simple AI: apply one transmutation to first eligible card.
     * During passing: prefer negative transmutations on high cards.
     * During play: prefer positive transmutations on low cards. */

    for (int s = 0; s < inv->count; s++) {
        const TransmutationDef *td = phase2_get_transmutation(inv->items[s]);
        if (!td) continue;

        /* Skip negative transmutations when not passing */
        if (!is_passing && td->negative) continue;
        /* Skip positive transmutations when passing (they'd be given away) */
        if (is_passing && !td->negative) continue;

        /* Find a card to transmute */
        bool ai_is_fog = (td->effect == TEFFECT_FOG_HIDDEN);
        int target = -1;
        if (is_passing && td->negative) {
            /* Pick highest-rank non-transmuted card */
            Rank best_rank = 0;
            bool found = false;
            for (int i = 0; i < hand->count; i++) {
                /* Skip restricted cards */
                Card c = hand->cards[i];
                if (c.suit == SUIT_CLUBS && c.rank == RANK_2) continue;
                if (c.suit == SUIT_SPADES && c.rank == RANK_Q && !ai_is_fog) continue;
                if (ai_is_fog) {
                    /* Fog: skip already-fogged, allow transmuted */
                    if (hts->slots[i].fogged) continue;
                } else {
                    if (transmute_is_transmuted(hts, i)) continue;
                }
                if (!found || hand->cards[i].rank > best_rank) {
                    best_rank = hand->cards[i].rank;
                    target = i;
                    found = true;
                }
            }
        } else {
            /* Pick lowest-rank non-transmuted card */
            Rank worst_rank = RANK_A;
            bool found = false;
            for (int i = 0; i < hand->count; i++) {
                /* Skip restricted cards */
                Card c = hand->cards[i];
                if (c.suit == SUIT_CLUBS && c.rank == RANK_2) continue;
                if (c.suit == SUIT_SPADES && c.rank == RANK_Q && !ai_is_fog) continue;
                if (ai_is_fog) {
                    if (hts->slots[i].fogged) continue;
                } else {
                    if (transmute_is_transmuted(hts, i)) continue;
                }
                if (!found || hand->cards[i].rank < worst_rank) {
                    worst_rank = hand->cards[i].rank;
                    target = i;
                    found = true;
                }
            }
        }

        if (target >= 0) {
            transmute_apply(hand, hts, inv, target, td->id, player_id);
            return; /* Apply only one per phase */
        }
    }
}
