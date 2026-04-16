---
name: Deserialization bounds check staleness
description: Hardcoded byte counts in protocol.c deser_player_view bounds checks go stale when NetPlayerView fields are added or resized
type: feedback
---

Deserialization bounds checks in `src/net/protocol.c` (e.g., `deser_player_view`) use hardcoded integer constants like `off + 36 > len`. These constants represent the sum of bytes for all fields read after the check, but they are NOT automatically updated when new fields are added.

**Why:** Found during Rogue/Duel card visibility audit (2026-04-04). Two new `read_card` calls (4 bytes) were added but the bounds check stayed at 36. Actual byte count was already 41 before the change (the original 36 was wrong too). After the change it should be 45.

**How to apply:** When reviewing any change that adds/removes fields to `NetPlayerView` serialization, always recount the bytes consumed after each bounds check and verify the hardcoded constant matches. The comment above the check usually has an arithmetic breakdown -- verify that too.
