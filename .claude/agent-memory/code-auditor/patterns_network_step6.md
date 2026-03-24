---
name: Network Step 6 audit patterns
description: Common issues found in server network loop implementation - incomplete command handlers, lost state across ticks, missing NULL checks on malloc
type: project
---

Key patterns found in Step 6 (Server Network Loop) audit:

1. **Incomplete multi-step command flows**: SELECT_CARD + CONFIRM pass flow was written with comments describing intent but no actual implementation. Watch for "TODO" or placeholder handlers that accept commands but don't mutate state.

2. **Lost state between ticks**: DUEL_PICK validates target player/index but doesn't store them, so DUEL_GIVE can't complete the swap. Any command split across multiple network messages needs persistent storage in ServerGame or Phase2State. File-scope statics were considered but not used.

3. **Missing malloc NULL checks**: ConnSlotInfo allocation in server_net.c had no NULL check. Always verify malloc results in server code -- a crash takes down all rooms.

4. **Dead code from double-condition checks**: CONFIRM handler checked `!pass_ready[seat]` twice -- once to enter the branch, once to reject inside it, making the handler always return false.

**How to apply:** When auditing multi-step network flows (any command that requires data from a previous command), verify the intermediate state is persisted in a struct field, not lost on the stack.
