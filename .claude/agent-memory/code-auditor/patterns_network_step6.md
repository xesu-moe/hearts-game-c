---
name: Network server audit patterns
description: Server game loop issues: broadcast-before-destroy ordering, stale ConnSlotInfo, duplicate room codes, plus earlier Step 6 patterns
type: project
---

Key patterns from server code audits:

**Full-lifecycle audit (2026-03-26), status: items 1-3 fixed as of latest review.**

1. ~~Broadcast ordering vs room destruction~~ FIXED: FINISHED rooms now survive one tick for broadcast, then `sv_cleanup_finished_rooms` destroys them.

2. ~~Stale ConnSlotInfo~~ FIXED: `sv_cleanup_finished_rooms` frees ConnSlotInfo before calling `room_destroy`. Abandoned rooms only destroyed when `connected_count == 0` (all ConnSlotInfo already freed via `sv_cleanup_connection`).

3. ~~Duplicate room codes~~ FIXED: `room_create_with_code` now checks `room_find_by_code`.

**New findings (2026-03-26 second audit):**

4. **Missing null-termination on `room_code` deserialization**: `NetMsgHandshake.room_code` is read as raw bytes (8 chars) without forcing a NUL terminator. `room_find_by_code` uses `strcmp`, risking read-past-buffer.

5. **Trick transmute info not sent to clients**: `net_build_player_view` zeroes `trick_transmutes` but server never populates it. Clients see no transmutation effects on trick cards.

6. **Every-tick broadcast**: `sv_broadcast_state` sends full state to all connected players every server tick (~16ms). No dirty flag or delta mechanism.

**Earlier Step 6 patterns:**

4. **Incomplete multi-step command flows**: Watch for placeholder handlers that accept commands but don't mutate state.

5. **Lost state between ticks**: Any command split across multiple messages needs persistent storage in ServerGame. Verify intermediate state is in a struct field, not lost on the stack.

6. **Missing malloc NULL checks**: ConnSlotInfo allocation originally had no NULL check (now fixed). Always verify malloc results in server code.

**How to apply:** When auditing server code changes, trace the full lifecycle: message received -> command applied -> state changed -> room status transition -> broadcast -> cleanup. Check ordering dependencies between stages.
