---
name: Client networking wiring patterns
description: Common issues in client-side networking integration: state machine transitions, phase sync between online UI and game state, NET_PLAYER_VIEW_MAX_SIZE undercount
type: project
---

## Key architecture decisions found (Step 20-21 audit)

1. **Command routing**: main.c drains queue, splits server-relevant (via `net_input_cmd_is_relevant`) from local-only, sends server commands, re-pushes local. Works correctly.

2. **State receive**: `state_recv_apply` in state_recv.c does seat remapping and populates both GameState and Phase2State. Applied in main.c after `client_net_update`.

3. **Online UI -> Game transition risk**: When first state update arrives during ONLINE_SUB_CREATE_WAITING or ONLINE_SUB_CONNECTED_WAITING, code sets `gs.phase = PHASE_MENU` but doesn't consume the state update yet. The state_recv block later overwrites phase to server's phase, but for one frame the phase is wrong and `game_state_start_game` is never called.

4. **NET_PLAYER_VIEW_MAX_SIZE**: Set to 1024 but `ser_player_view` uses it as a hard minimum check. Actual serialized size with max phase2 data could approach or exceed this. Should use actual buffer size instead.

**Why:** These are recurring patterns when networked state machines share the same game loop.
**How to apply:** When reviewing online UI transitions, always check that state consumption and phase transitions happen atomically in the same frame.
