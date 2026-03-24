# Code Auditor Memory Index

- [patterns_contract_logic.md](patterns_contract_logic.md) — Transmutation/contract effect flow patterns: ordering, pending-winner overwrite, visual sync fragility
- [patterns_cv_idx_sync.md](patterns_cv_idx_sync.md) — cv_idx stale reference risk when sync_needed fires during multi-step animations
- [patterns_draft_system.md](patterns_draft_system.md) — Draft pool generation pitfalls: modulo-self fill bug, duplicate timer constants
- [patterns_variable_count_migration.md](patterns_variable_count_migration.md) — Constant-to-variable migration: grep ALL refs, watch for array sizing (overflow), loop bounds, UI caps
- [patterns_network_step6.md](patterns_network_step6.md) — Network loop: incomplete multi-step command handlers, lost state across ticks, missing malloc NULL checks
