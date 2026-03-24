---
name: Draft pool generation pitfalls
description: Modulo-self bug in fallback fill loop; duplicate timer constants between contract_logic.h and pass_phase.h
type: project
---

**Bug pattern:** `array[i] = array[i % i]` is always index 0 and division-by-zero if i==0. Found in draft_generate_pool fallback fill loop (contract_logic.c:175).

**Duplicate constants:** DRAFT_TIMER_SECONDS (contract_logic.h) and PASS_CONTRACT_TIME (pass_phase.h) both define 30.0f for the same draft timeout. One should reference the other.

**How to apply:** When reviewing pool/selection generation code, check fallback/fill loops for self-referential modulo. When reviewing timer-driven flows, verify that the timer constant used for countdown and the constant used for timeout comparison are the same symbol.
