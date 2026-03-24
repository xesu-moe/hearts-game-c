---
name: Transmutation effect flow patterns
description: Recurring issues found in transmutation/contract effect implementations — bounds checks, visual sync ordering, pending-winner overwrite
type: project
---

contract_on_trick_complete has strict ordering requirements:
1. PREVENT_MOON check runs BEFORE winner's points_taken is updated
2. suit_seen[] updates AFTER all per-player checks (so first-of-suit works)
3. Streak reset for non-winners is in the all-player loop
4. trick_number is gs->tricks_played - 1 because game_state_complete_trick already incremented it

**Why:** These ordering constraints prevent subtle bugs where state mutation interferes with condition checking within the same trick.

**How to apply:** When reviewing changes to contract_on_trick_complete, verify section ordering is preserved. The code has labeled sections (1-5) for this purpose.

Also watch for: hearts_broken_at_trick_start snapshot in turn_flow.c (only taken when num_played == 0).

---

**Pending winner fields can be silently overwritten.** When multiple cards with the same effect appear in one trick, `transmute_on_trick_complete` overwrites the pending winner. Only the last card's winner takes effect. The `TransmuteRoundState` stores a single `int` per effect type.

**Per-player flags must be consumed for ALL players, not just the winner.** Effects like Binding set a flag on the winner, meaning to affect their *next* trick. If a player with an active flag does NOT win the next trick, the flag must still be consumed (it was their "next trick" — they just lost it). Only consuming on the winner leaves stale flags on other players. Pattern: consume all active flags first, then let transmute_on_trick_complete re-set for the new winner.

**Visual index usage after game state mutation is fragile.** After `transmute_swap_between_players` or `game_state_complete_trick`, the `hand_visuals[][]` mapping may be stale. Always clear visual state *before* mutating game state, or iterate all visuals to reset. `sync_needed = true` triggers rebuild in render_update, not immediately.
