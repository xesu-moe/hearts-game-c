# Competitive AI — Hearts Card Game

## Overview

This document defines the complete decision-making logic for the **Competitive**
difficulty AI in the Hearts card game. The AI is rule-based (heuristic), using
approximately 50 weighted rules across all decision phases. Research shows that a
strong heuristic agent with ~20 core rules can win over 50% of games; this
document goes deeper to cover edge cases and advanced plays.

The AI maintains full state tracking: every card played, every void detected,
Q♠ location at all times, and cumulative scores. All decisions are deterministic
given the same game state — no randomness.

---

## Table of Contents

1. [State Tracking (Prerequisites)](#1-state-tracking-prerequisites)
2. [Passing Phase](#2-passing-phase)
3. [First Trick (2♣ Lead)](#3-first-trick-2-lead)
4. [Trick Play — Leading](#4-trick-play--leading)
5. [Trick Play — Following Suit](#5-trick-play--following-suit)
6. [Trick Play — Sloughing (Off-Suit)](#6-trick-play--sloughing-off-suit)
7. [Shoot the Moon](#7-shoot-the-moon)
8. [Endgame (Last 3–4 Tricks)](#8-endgame-last-34-tricks)
9. [Score-Based Adaptation](#9-score-based-adaptation)
10. [Card Evaluation Function](#10-card-evaluation-function)

---

## 1. State Tracking (Prerequisites)

The competitive AI **must** maintain these data structures throughout the entire
hand. Every rule below depends on this information being accurate.

### 1.1 — Played Cards

```
bool played[52];  // true once a card has been played in any trick
```

Updated after every trick resolves. Used to deduce what remains in opponents'
hands.

### 1.2 — Void Map

```
bool player_void[4][4];  // player_void[player][suit] = true if player showed out
```

Set to `true` the moment a player fails to follow suit. Once void, always void
for the rest of the hand. **This is the single most valuable piece of opponent
information.**

### 1.3 — Q♠ Location

At all times, the AI knows exactly one of these states:

| State                  | Meaning                                    |
|------------------------|--------------------------------------------|
| `QS_IN_MY_HAND`       | I hold the Q♠                              |
| `QS_PLAYED`           | Q♠ has been played and is no longer a threat|
| `QS_PASSED_TO_X`      | I passed Q♠ to player X this hand          |
| `QS_RECEIVED_FROM_X`  | I received Q♠ from player X in passing     |
| `QS_LOCATION_UNKNOWN` | Q♠ is in an opponent's hand, identity unknown|

When `QS_PASSED_TO_X`, the AI knows exactly who has it (unless they passed it
on during an "across" pass cycle, which cannot happen — you only pass once).

### 1.4 — Suit Counts

```
int remaining_in_suit[4];  // how many cards of each suit are still unplayed
```

Starts at 13 per suit, decremented as cards are played. Critical for:
- Flushing decisions (how many spade leads to drain them all)
- Endgame calculations (can I safely lead this suit?)

### 1.5 — Hearts Taken Per Player

```
int hearts_taken[4];     // how many heart points each player has this hand
bool has_queen[4];       // which player took Q♠ (if played)
```

Used for moon detection and targeted dumping.

### 1.6 — Game Scores

```
int game_score[4];       // cumulative score across all hands
```

Drives aggression scaling and leader targeting (Section 9).

### 1.7 — Pass Analysis

After receiving passed cards, record:

```
int pass_same_suit_count;  // how many of the 3 received cards share a suit
bool pass_all_low;         // were all 3 passed cards rank 7 or below
int pass_direction;        // 0=left, 1=right, 2=across, 3=hold
```

Used to infer opponent intent (voiding vs. moon attempt).

---

## 2. Passing Phase

The AI selects 3 cards to pass. The process is sequential:

### 2.0 — Check for Moon Hand First

Before selecting passes, evaluate if this hand can shoot the moon (see Section
7.1). If yes, pass defensively to protect the moon hand — pass low cards in
weak suits to shore up control. **Do not** pass high cards that are needed for
the moon.

### 2.1 — Q♠ Decision

**Keep Q♠ if:**
- You have 4+ spades total (including Q♠). The low spades act as a buffer
  during spade flushes. You know exactly where the Queen is at all times.
- Pass direction is LEFT (passing Q♠ left means that player plays *before* you
  in many tricks — they control when spades are led and can dump Q♠ on you).

**Pass Q♠ if:**
- You have 0–2 other spades. You cannot survive a spade flush.
- Pass direction is RIGHT (preferred). The recipient plays *after* you, so you
  play your card before they can dump Q♠ back.
- Pass direction is ACROSS (acceptable). Neutral position.

### 2.2 — A♠ / K♠ Decision

**Keep A♠ / K♠ if:**
- You also hold Q♠ (they help flush other spades to make Q♠ safe).
- You have 4+ total spades. The high spades will survive flushing.
- You have 0–1 spades below Q♠ **and** might receive Q♠ from a pass. Keeping
  A♠/K♠ lets you take spade tricks safely above the Queen (since you'd also
  have the Queen, no one can dump it on you). **But** if you don't hold Q♠ and
  have few low spades, A♠/K♠ are extremely dangerous.

**Pass A♠ / K♠ if:**
- You do NOT hold Q♠ **and** you have fewer than 3 low spades. These cards will
  eat the Queen when someone leads spades.

### 2.3 — Void Targeting

Identify the shortest non-spade suit in hand. If it has 1–3 cards, pass all of
them (or as many as needed to reach 3 total passed cards) to create a void.

**Priority order for voiding:**
1. **Clubs** — Most valuable void. The first trick is always clubs; being void
   after trick 1 gives maximum flexibility for the rest of the hand. However:
   **NEVER pass your last club.** Keep exactly 1 club minimum. Showing void on
   trick 1 alerts all opponents to your strategy.
2. **Diamonds** — Second most valuable. No "safe" diamond tricks exist (unlike
   the protected first club trick), so opponents void diamonds frequently too.
3. **Hearts** — Void hearts only if you have exclusively high hearts (A♥ K♥ Q♥)
   with no low hearts to duck under. Otherwise, keep low hearts as shields.

**Never void spades** by passing low spades. Low spades are lifelines against
Q♠ flushes. If you void spades and then receive Q♠, you are stuck with 13
guaranteed points and no way to duck.

### 2.4 — Low Hearts Retention

**Always keep hearts ranked 2–6 if possible.** After hearts are broken, hearts
will be led frequently. Low hearts let you follow suit without winning the
trick. The 2♥ and 3♥ are the most valuable defensive cards in the game against
heart leads.

Pass high hearts (J♥, Q♥, K♥, A♥) **unless** you have 4+ total hearts
including several low ones — in that case, the low hearts protect the high ones.

**Exception — A♥ retention:** The A♥ is a dual-purpose card. It is a guaranteed
moon-stopper (play it to take a heart trick and block any opponent's moon
attempt). Keep A♥ if you have any suspicion of a moon attempt or if you have a
strong enough hand to consider shooting yourself.

### 2.5 — Dangerous Aces

Pass aces in suits where you have few other cards (1–2 cards total in the suit).
An ace in a short suit will win a trick loaded with dumped hearts. Aces in long
suits (4+) are safer because you have low cards to play first.

### 2.6 — Poison Pair Combinations

When pass direction is LEFT (most punishing, since the recipient leads before
you):

| Pass Combination        | Effect on Recipient                        |
|-------------------------|--------------------------------------------|
| 2♣ + A♣                | Loses first-trick lead AND stuck with A♣   |
| A♠/K♠ + a club         | Stuck with queen-magnet + disrupted void   |
| Low ♥ + High ♥ (>your) | Can't dump the high, low ruins a moon try  |
| 3 same-suit high cards | Forces recipient to eat tricks in that suit |

### 2.7 — Read Incoming Pass

After receiving cards, update internal state:

- **3 cards same suit:** Passer is building a void in that suit. Flag it. Do
  NOT lead that suit early — they will dump penalties on you.
- **All low cards (≤7):** Possible moon attempt. Increase moon vigilance
  (Section 7.3).
- **Q♠ received:** Immediately re-evaluate spade holdings. Count your spade
  buffer. If buffer is < 3, you need to void another suit ASAP for a dump
  route.

---

## 3. First Trick (2♣ Lead)

The first trick has special rules: the player holding 2♣ must lead it, and no
hearts or Q♠ may be played (unless that is all you have, which is near
impossible).

### 3.1 — If You Lead (Hold 2♣)

You must play 2♣. No decision to make. After this trick resolves, you will NOT
have the lead (unless you played the only club, which is trivially rare).

### 3.2 — If You Follow

**Goal:** Shed the highest safe club you can, OR play strategically low.

- **If you want to win this trick** (to control the lead for trick 2): play A♣.
  Do this if you have a good low card to lead on trick 2 that advances your
  void strategy. Winning trick 1 is safe — no points can be in it.
- **If you do NOT want the lead:** play your highest club (gets rid of a
  dangerous card safely — no penalty cards are in the trick). Exception: keep
  at least 1 club if you have 2+ clubs remaining (don't void clubs on trick 1
  — you'd reveal the void to everyone).
- **If you have no clubs:** Play a non-heart, non-Q♠ card. Dump your highest
  diamond or highest safe card. This is the one time being void in clubs on
  trick 1 happens — but the rules forbid hearts/Q♠ here, so dump accordingly.

---

## 4. Trick Play — Leading

When the AI wins a trick and must lead the next one.

### 4.1 — General Lead Priority

Evaluate every legal lead card using the scoring function (Section 10), but the
following heuristic priorities apply:

**Early game (tricks 2–5):**

1. **Lead from void-target suit (high cards first).** You want to empty this
   suit from your hand. Lead high because: nobody can dump Q♠ on a trick you
   lead (Q♠ is only dangerous when someone else leads and you're forced to
   follow), and the worst you take is a heart or two — far better than 13.
2. **Flush spades** if you do NOT hold Q♠, K♠, or A♠. Lead your lowest spade
   repeatedly to force the Queen out. Count remaining spades: if you estimate
   only 2–3 spade leads needed to drain them, commit to flushing.
3. **Lead low in a safe long suit** (a suit where you have 4+ cards including
   2s/3s). This lets you shed a trick cheaply and may draw out opponents' high
   cards.

**Mid game (tricks 6–9):**

1. **Lead spades if Q♠ is still out** and you can duck under it. Force the
   Queen.
2. **Lead hearts (if broken) with low hearts** to make opponents eat points
   while you duck.
3. **Lead from your longest remaining suit** to maintain control and avoid
   getting stuck.

**Late game (tricks 10–13):** See Section 8.

### 4.2 — Leading Spades (Flushing)

This is one of the most important competitive tactics. The goal is to force Q♠
out of whoever holds it.

**When to flush:**
- You do NOT hold Q♠
- Q♠ has NOT been played yet
- You have low spades (below J♠) to lead with

**How to flush:**
- Lead your lowest spade. This forces all players to follow suit. The Q♠ holder
  must either play Q♠ (losing 13 points) or play under it (but eventually runs
  out of ducking room).
- Track `remaining_in_suit[SPADES]`. Each spade lead drains 4 spades from the
  game. With 13 total spades, after ~3 spade leads, Q♠ will almost certainly
  be forced out.

**When you HOLD Q♠ (and want to flush for safety):**
- Lead with A♠ first, then K♠. You take the trick, but since YOU have the
  Queen, no one can dump it on you. Each high spade lead drains opponents'
  spades.
- After 2–3 leads, opponents are void in spades. Now Q♠ is harmless in your
  hand — no one can lead spades to force it out, and you can dump it when
  off-suit later.
- **Threshold:** If you hold Q♠ + 4 other spades (5 total), there are only 8
  among 3 opponents (~2.7 each). Two A♠/K♠ leads will likely drain most of
  them.

### 4.3 — Leading Hearts

Hearts can only be led after they are broken (a heart was played on a prior
trick).

**Lead hearts when:**
- You have low hearts (2♥–6♥) and want to force opponents to eat points.
- You suspect an opponent is void in hearts and will be forced to take them in
  other suits instead — leading hearts is then safer.

**Do NOT lead hearts when:**
- You have only high hearts remaining. You will take your own trick.
- Q♠ is still out and you need to focus on flushing spades.

### 4.4 — Leading to Avoid Getting Stuck

As the hand progresses and hands shrink, the risk of "getting stuck with the
lead" increases. If you have only 1 suit left, you're forced to lead it. If
opponents are void, they dump hearts on every trick you lead.

**Prevention:** Keep low cards in at least 2 different suits for the endgame.
When choosing which cards to play in mid-game tricks, prefer shedding cards
that leave you with diversity, not a single long suit.

---

## 5. Trick Play — Following Suit

You must play a card of the led suit if you have one.

### 5.1 — Play-Under Rule (Default)

Play the highest card that LOSES the trick. This safely sheds a high card
without taking the trick.

**Example:** Spades led, current winner is 10♠. You hold 3♠, 7♠, J♠, K♠.
Play the 7♠ (highest card below 10♠). This gets rid of a middling card without
risk.

### 5.2 — Second-Highest Unload

When following suit and you CANNOT duck under the current winner, play your
HIGHEST card in that suit. Rationale: you are taking the trick regardless, so
minimize future risk by dumping the most dangerous card.

**Example:** Hearts led, current winner is K♥. You hold Q♥ and A♥.
Play A♥ — you take the trick either way, and A♥ is more dangerous to hold.

### 5.3 — Last-to-Play Optimization

If you are the 4th (last) player in the trick:

- **If the trick has NO penalty cards:** play your HIGHEST card in the led suit.
  You can safely take a clean trick and shed a high card. You also get lead
  control for the next trick.
- **If the trick HAS penalty cards and you can duck:** play the highest card
  that still ducks.
- **If the trick HAS penalty cards and you MUST take it:** play the highest
  card. Minimize future damage.

### 5.4 — Spade Following (Q♠ Not Yet Played)

When spades are led and Q♠ is still out:

- **You hold Q♠:** Play the highest spade BELOW Q♠ (J♠ or lower). Do NOT play
  Q♠ unless you are forced (it's your only spade).
- **You do NOT hold Q♠, and the current trick winner is below Q♠:** Play your
  lowest spade. Let someone else eat the Queen.
- **You do NOT hold Q♠, and A♠ or K♠ is already played in the trick:** You can
  safely play high spades (even K♠) because Q♠ will go to whoever played the
  Ace/King.
- **Q♠ is already in the trick:** Play your HIGHEST spade. You aren't taking
  the Queen (it goes to whoever wins), and you shed a dangerous card.

### 5.5 — Spade Following (Q♠ Already Played in a Previous Trick)

Once Q♠ is out of the game, spades are essentially a safe suit (only risk is a
heart dump by a void player). Play normally — duck when possible, shed high
when forced.

### 5.6 — Safe Trick-Taking for Moon Blocking

If the moon alarm is active (see Section 7.3), **deliberately take a heart
trick** if you can do so cheaply (1–4 points). This blocks the moon. Play your
A♥ or highest heart to guarantee you win the trick and break the moon attempt.

### 5.7 — Following in a Suit You Want to Void

If you are following in a suit you want to void, play your HIGHEST card in that
suit (to shed it), even if it means taking the trick. Taking a clean trick
(no hearts, no Q♠) is an acceptable cost for advancing a void.

---

## 6. Trick Play — Sloughing (Off-Suit)

When you are void in the led suit, you may play ANY card. This is the most
impactful decision point in the game.

### 6.1 — Dump Priority Order

When off-suit, dump the first applicable card from this list:

| Priority | Card           | Condition                                          |
|----------|----------------|----------------------------------------------------|
| 1        | Q♠             | Always. 13 points instantly on the trick-taker.    |
| 2        | A♠             | If Q♠ is NOT in your hand and NOT yet played.      |
| 3        | K♠             | Same condition as A♠.                               |
| 4        | A♥             | Unless you need it as a moon-stopper (Section 7.3).|
| 5        | K♥             | High heart dump.                                    |
| 6        | Q♥             | High heart dump.                                    |
| 7        | J♥             | High heart dump.                                    |
| 8        | Any Ace        | In a suit where you have few cards (dangerous lead magnet). |
| 9        | Any King       | Same reasoning.                                     |
| 10       | Card advancing void | From a suit you are 1 card away from voiding.  |

### 6.2 — Targeted Dumping (WHO Gets the Points)

When you have a choice, dump penalty cards on tricks being won by:

1. **The game leader** (highest cumulative score). Always preferred.
2. **The player most likely to win the trick.** Check the current trick state.
3. **NOT on a player attempting a moon** (unless you're trying to block their
   moon — in that case, dump a single small heart on a DIFFERENT player to
   break the sweep, see Section 7.3).

### 6.3 — First Trick Sloughing (Special Case)

On the first trick (clubs led), hearts and Q♠ are usually prohibited. If your
game variant allows Q♠ on trick 1 when void in clubs, dump it immediately —
this is the safest possible dump since it's unexpected and most players assume
trick 1 is safe.

If hearts/Q♠ are prohibited: dump your highest non-club, non-heart card. 
Prefer K♠ or A♠ since they're the most dangerous cards after Q♠ herself.

### 6.4 — Don't Reveal Void Too Early (Information Control)

If this is the SECOND trick and you are void in the led suit, consider what you
dump carefully. Dumping Q♠ on trick 2 is a strong play. But dumping a random
heart just because you can reveals your void for minimal gain. If the trick is
clean (no penalty cards on it yet), consider dumping a high card from another
suit you want to thin, rather than a heart — save the heart dumps for when they
land on the right opponent.

---

## 7. Shoot the Moon

### 7.1 — Evaluating a Moon Hand (Pre-Pass)

Score the hand for moon potential. Count:

| Factor                    | Weight | Notes                              |
|---------------------------|--------|------------------------------------|
| A♥ in hand                | +3     | Essential — controls heart suit    |
| K♥ in hand                | +2     |                                     |
| Q♥ in hand                | +2     |                                     |
| J♥ in hand                | +1     |                                     |
| Other hearts (each)       | +0.5   | More hearts = more control         |
| Q♠ in hand                | +3     | Must take it anyway for moon       |
| A♠ in hand                | +2     | Helps secure Q♠                    |
| K♠ in hand                | +1     |                                     |
| Aces in other suits (each)| +2     | Can win tricks to maintain lead    |
| Kings in other suits      | +1     |                                     |
| Void in a suit            | -2     | Can't take tricks in that suit     |
| Cards below 5 (non-heart) | -0.5   | Weak trick-winners                 |

**Moon threshold:** If score ≥ 14, consider shooting. If score ≥ 18, commit.

**Pre-pass adjustment:** When shooting, pass your LOWEST cards in your weakest
suit (to shore it up), NOT your high cards. Contrary to normal passing logic.

### 7.2 — Executing a Moon Attempt

**Phase 1 — Stealth (Tricks 1–4):**
- Win tricks using aces in side suits (clubs, diamonds). Do NOT play hearts yet.
- This looks like normal "clearing high cards" play and does not alert opponents.

**Phase 2 — Commitment (Tricks 5–9):**
- Start taking heart tricks aggressively. Lead with high hearts when you have
  the lead.
- Take Q♠ if you haven't yet. Lead A♠ or K♠ to draw it out, or win a spade
  trick that includes it.

**Phase 3 — Sweep (Tricks 10–13):**
- By now, you should have most hearts. Opponents may realize the moon attempt.
- Use remaining aces/kings to win any trick that has a stray heart.
- If you have the lead, lead your remaining hearts to collect the rest.

### 7.3 — Moon Detection (Opponent)

**Trigger the moon alarm when any single opponent has:**
- Taken 5+ hearts **AND** taken Q♠, OR
- Taken 8+ hearts (even without Q♠, they are likely trying)

Reduce the threshold if:
- The opponent received all low cards in the pass (pre-flagged in 2.7)
- The opponent has been winning tricks aggressively in non-heart suits (stealth)

### 7.4 — Moon Blocking

Once the moon alarm is active, **the sole priority is to take at least 1 heart
trick.** Everything else is secondary.

**Blocking tactics (in order of preference):**

1. **Play A♥ on a heart lead.** Guarantees you take the trick and 1+ hearts.
   Moon is broken.
2. **Dump a low heart on a trick another non-moon player is winning.** The moon
   player doesn't get that heart. Moon is broken. Cost: 1 point to the
   recipient (much better than 26 each).
3. **Win any trick and lead low hearts.** Force the non-moon players to take at
   least one heart between them.
4. **If you are void in the led suit, dump a low heart (2♥, 3♥) on ANY trick
   that the moon player is NOT winning.** Even 1 heart going to the wrong
   player ruins the moon.

**Never cooperate with the moon** unless the moon player is losing badly AND
the AI has a massive score lead (see Section 9.4 for this rare exception).

### 7.5 — Aborting a Moon Attempt

If the AI is attempting a moon and ANY other player takes a heart:

1. **Immediately** switch to point-avoidance mode.
2. Dump all remaining high cards at the first off-suit opportunity.
3. The hearts already taken are sunk costs. Minimize further accumulation.

**Do NOT half-commit.** A failed moon with 10 hearts taken = 10+ points. Abort
cleanly.

---

## 8. Endgame (Last 3–4 Tricks)

The endgame is where most points are won or lost. Opponents are short-suited,
voids are everywhere, and every trick is dangerous.

### 8.1 — Stuck-Lead Prevention

If you win a late trick, you lead the next one. If you have only 1 suit left,
opponents void in that suit will dump hearts on every trick you lead.

**Prevention rules (applied throughout the hand):**
- During mid-game, prefer plays that keep you with low cards in at least 2
  different suits.
- Avoid taking tricks after trick 9 unless the trick is clean (no penalty
  cards) and you have a safe next lead.

### 8.2 — Last-Trick Calculation

When 4 or fewer cards remain per player, the AI can do **perfect endgame
calculation.** With the played-cards array, the AI knows exactly which cards
each opponent holds (by elimination). It can simulate all possible play
orderings and choose the line that minimizes its own points taken.

**Implementation:** With ≤4 cards per player, there are at most 4! × 4 =
96 possible trick outcomes. Brute-force search is trivial.

### 8.3 — Sandbagging

In the last 2–3 tricks, sometimes it is better to intentionally take a small
heart trick (1–3 points) now to avoid being forced into a larger heart trick
later. Evaluate both options:

- Option A: Duck this trick, risk eating 4+ points next trick when stuck.
- Option B: Take this trick for 1–2 points, lead a safe card next.

Choose the option with lower expected point total.

### 8.4 — Q♠ Still Out in Endgame

If Q♠ has NOT been played and only 3–4 tricks remain:

- **If you hold Q♠:** You MUST void another suit immediately to dump it. If you
  cannot void, play the lowest spade possible on spade tricks and pray.
- **If you do NOT hold Q♠:** Do NOT lead spades unless you have only low spades
  and want to flush it out now. Otherwise, let someone else trigger the Queen.
- **If all spades except Q♠ are played:** The holder MUST play Q♠ on the next
  spade trick. Lead a low spade to force it.

---

## 9. Score-Based Adaptation

The competitive AI adjusts its strategy based on the overall game scores.

### 9.1 — Aggression Calculation

```
score_gap = ai_score - min_opponent_score
```

| score_gap | Aggression Level | Behavior                              |
|-----------|------------------|---------------------------------------|
| < -20     | Low (winning)    | Play conservatively. Avoid all risk.  |
| -20 to 0  | Normal           | Standard competitive play.            |
| 0 to +15  | High             | Accept 1-3 point risks for position.  |
| > +15     | Desperate        | Attempt moons more readily. Target leader aggressively. |

### 9.2 — Leader Targeting

When dumping penalty cards, always prefer targeting the player with the
**lowest score** (the game leader — remember, lowest = winning).

**Exception:** If a player is close to 100 points, dumping on them ends the
game. Only do this if the AI has the lowest score and would win.

### 9.3 — Near-100 Awareness

If any player (including the AI) is within 26 points of 100:

- A successful moon by that player pushes everyone else 26 points closer to
  100 — or ends the game. Be hyper-vigilant about moon blocking.
- If the AI is near 100, play ultra-conservatively. Do NOT attempt moons. Take
  zero risks. A single Q♠ catch could end the game.

### 9.4 — Cooperative Moon (Rare)

In extreme situations where:
- The AI has a massive lead (lowest score by 30+ points), AND
- A losing player (near 100) is attempting a moon, AND
- The moon would push other opponents closer to 100 but the AI would still win

The AI may **choose not to block** the moon. This is a calculated gamble to end
the game sooner while still winning. Only apply this when the math is clear.

---

## 10. Card Evaluation Function

For every legal card play, the AI computes a score. The card with the highest
score is played.

### 10.1 — Scoring Formula

```
score(card) =
    W_SAFETY     * trick_safety(card)
  + W_VOID       * void_progress(card)
  + W_SHED       * dangerous_card_shed(card)
  + W_QUEEN      * queen_safety(card)
  + W_MOON       * moon_factor(card)
  + W_TARGET     * opponent_targeting(card)
  + W_INFO       * information_hiding(card)
  + W_ENDGAME    * endgame_safety(card)
```

### 10.2 — Factor Definitions

**trick_safety(card):** [-10 to +10]
- +10 if the card guarantees NOT taking the trick.
- 0 if uncertain.
- -10 if the card guarantees taking a trick with Q♠ or 4+ hearts in it.

**void_progress(card):** [0 to +5]
- +5 if playing this card completes a void.
- +3 if it's the second-to-last card in a suit being voided.
- 0 if it doesn't advance any void.

**dangerous_card_shed(card):** [0 to +8]
- +8 for Q♠ dump.
- +6 for A♠/K♠ dump (while Q♠ is still out).
- +4 for A♥/K♥/Q♥ dump.
- +2 for other aces/kings.
- 0 for safe low cards.

**queen_safety(card):** [-10 to +5]
- -10 if this play exposes you to taking Q♠ (e.g., playing A♠ when Q♠ is out).
- +5 if this play eliminates Q♠ risk (e.g., Q♠ is already in the trick).
- 0 if Q♠ is already played or irrelevant.

**moon_factor(card):** [-5 to +5]
- +5 if the AI is shooting and this card wins a needed heart trick.
- +5 if this card blocks an opponent's moon (takes a heart away from them).
- -5 if this card helps an opponent's moon attempt.
- 0 if no moon is in progress.

**opponent_targeting(card):** [0 to +3]
- +3 if this card dumps points on the game leader.
- +1 if it dumps on any non-leading opponent.
- 0 if no targeting applies.

**information_hiding(card):** [-3 to 0]
- -3 if this play reveals a void on trick 1 or 2 unnecessarily.
- -1 if it reveals a void early with minimal gain.
- 0 otherwise.

**endgame_safety(card):** [-5 to +5]
- +5 if this card leaves you with low cards in 2+ suits for endgame.
- -5 if this card leaves you stuck with only high cards in 1 suit.
- Only relevant in tricks 8+.

### 10.3 — Weight Scaling by Game Phase

| Weight       | Early (1-4) | Mid (5-9) | Late (10-13) |
|--------------|-------------|-----------|--------------|
| W_SAFETY     | 5           | 7         | 10           |
| W_VOID       | 8           | 5         | 2            |
| W_SHED       | 6           | 7         | 4            |
| W_QUEEN      | 9           | 9         | 9            |
| W_MOON       | 3           | 6         | 8            |
| W_TARGET     | 2           | 3         | 3            |
| W_INFO       | 4           | 1         | 0            |
| W_ENDGAME    | 0           | 3         | 8            |

Weights shift as the hand progresses: early game prioritizes voiding and
information hiding; late game prioritizes safety and endgame calculation.

### 10.4 — Tie-Breaking

If two cards have equal scores, prefer:
1. The card with higher rank (sheds a more dangerous card).
2. The card from the suit with more remaining cards in the AI's hand (preserves
   suit diversity).

---

## Appendix A — Quick Decision Flowcharts

### A.1 — "Should I Keep Q♠?" (Passing Phase)

```
Hold Q♠?
├── I have 4+ spades total → YES, keep Q♠
├── I have 3 spades total
│   ├── Pass direction is LEFT → YES, keep (too risky to give left)
│   └── Pass direction is RIGHT/ACROSS → PASS Q♠
├── I have 0-2 other spades → PASS Q♠
└── I'm shooting the moon → YES, keep Q♠
```

### A.2 — "What Should I Lead?" (Has the Lead)

```
My lead:
├── Am I shooting the moon?
│   ├── YES → Lead my highest winner in a side suit (stealth) or hearts (commit)
│   └── NO → continue
├── Q♠ still out AND I don't hold it?
│   ├── I have low spades → Lead lowest spade (flush)
│   └── I have no spades → Lead from void-target suit
├── I have a suit I'm voiding?
│   └── Lead highest card in that suit
├── Hearts broken AND I have low hearts?
│   └── Lead low heart (force opponents to eat points)
└── Default → Lead lowest card in longest suit
```

### A.3 — "What Do I Dump When Off-Suit?"

```
I'm void in led suit:
├── I hold Q♠ → DUMP Q♠ (always)
├── Q♠ still out → DUMP A♠, then K♠
├── Moon alarm active?
│   ├── Moon player is winning this trick → Dump low heart on DIFFERENT trick
│   └── Moon player is NOT winning → Dump anything (normal priority)
├── DUMP highest heart I have
├── DUMP aces/kings from short suits
└── DUMP card that advances a void
```

---

## Appendix B — Constants and Thresholds

These values should be tuned through playtesting. Starting recommendations:

```c
#define MOON_SCORE_THRESHOLD      14    // Min moon-hand score to consider
#define MOON_COMMIT_THRESHOLD     18    // Min moon-hand score to commit
#define MOON_ALARM_HEARTS         5     // Hearts taken by 1 player to trigger alarm
#define MOON_ALARM_WITH_QUEEN     5     // Hearts + Q♠ to trigger alarm
#define MOON_ALARM_NO_QUEEN       8     // Hearts without Q♠ to trigger alarm
#define SPADE_BUFFER_SAFE         4     // Spades needed to safely hold Q♠
#define SPADE_BUFFER_MIN          3     // Minimum spades to consider keeping Q♠
#define LOW_HEART_MAX_RANK        6     // 2-6 count as "low hearts"
#define VOID_REVEAL_PENALTY_TRICK 2     // Don't reveal voids before this trick
#define ENDGAME_START_TRICK       10    // When to activate endgame logic
#define PERFECT_CALC_THRESHOLD    4     // Cards per player to trigger brute-force
#define DESPERATE_SCORE_GAP       15    // Gap to trigger desperate aggression
#define COOPERATIVE_MOON_LEAD     30    // Score lead needed to allow opponent moon
```
