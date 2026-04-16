---
name: Variable count migration pattern
description: When converting a compile-time constant to a runtime variable, all references must be audited -- especially array sizes, loop bounds, and UI caps
type: feedback
---

When a constant like PASS_CARD_COUNT is made variable at runtime, the backwards-compat #define masks unmigrated references. Pattern to watch for:

1. **Array sizing**: Stack arrays sized with the old constant become too small for larger values (buffer overflow)
2. **Loop bounds**: Iterations using old constant skip or overrun elements
3. **UI caps**: Selection limits, display counters using old constant show wrong values
4. **Struct fields**: Fixed-size arrays in structs (especially in headers used across files) must use MAX variant
5. **Function calls**: Functions passing the old constant instead of runtime value

**Why:** Found 15+ unmigrated PASS_CARD_COUNT references after it was made variable via dealer system. Three were buffer overflows.

**How to apply:** When reviewing any constant-to-variable migration, grep for ALL references to the old constant. Flag any that should use the runtime value or the MAX variant for sizing.
