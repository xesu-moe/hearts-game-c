---
name: cv_idx stale reference during multi-step animations
description: CardVisual indices stored in TurnFlow become invalid if sync_needed fires mid-animation, since sync_hands() rebuilds the card pool
type: project
---

Multi-step animation flows (Rogue reveal, Duel exchange) store `cv_idx` values (indices into `rs->cards[]`) across several FlowStep states. These indices become stale if `sync_needed` triggers `sync_hands()`, which rebuilds the entire card visual pool.

**Why:** `sync_hands()` reallocates card visuals from scratch. Any stored cv_idx from before the sync points to a different card (or garbage).

**How to apply:** When auditing animation flows that span multiple FlowSteps:
1. Verify no code path between the cv_idx capture and its final use sets `sync_needed = true`
2. Check that `pile_anim_in_progress` and `pass_anim_in_progress` guards in `render_update` don't inadvertently delay a sync that was set before the animation started
3. Watch for `hand_visuals[p][i]` lookups that assume sync has already run -- if sync is blocked, the indices may be pre-mutation

Also: `render_hit_test_opponent_card` and opponent hover clear loop in render.c lack bounds checks on indices from `hand_visuals[]`. Pre-existing pattern but should be fixed.
