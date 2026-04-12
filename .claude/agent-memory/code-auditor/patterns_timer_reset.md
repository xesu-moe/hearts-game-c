---
name: Timer reset on substate transitions
description: Server-side timers (pass_phase_timer, duel_timer) must be reset at EVERY entry point to a timed substate, not just timeout paths
type: feedback
---

When adding server-side timeouts, every code path that transitions INTO a timed substate must reset the corresponding timer to 0.0f. Common miss: command handlers and post-effect transitions reset the substate enum but forget the timer field.

**Why:** Stale timer values from previous substates or timed-out phases carry over and cause instant or premature timeouts on the next entry.

**How to apply:** When reviewing timeout additions, grep for ALL assignments to the substate enum value (e.g., `SV_PLAY_DUEL_PICK_WAIT`) and verify each one also resets the timer. Check both the tick functions AND the command handlers (`server_game_apply_cmd`).

Found instances in this project:
- `duel_timer` not reset when entering DUEL_PICK_WAIT from rogue handler (line ~604)
- `pass_phase_timer` not reset in INPUT_CMD_DEALER_DIR / INPUT_CMD_DEALER_AMT command handlers, causing shared timer to bleed across dealer substates
