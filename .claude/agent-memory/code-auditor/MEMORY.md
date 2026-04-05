# Code Auditor Memory Index

- [patterns_contract_logic.md](patterns_contract_logic.md) — Transmutation/contract effect flow patterns: ordering, pending-winner overwrite, visual sync fragility
- [patterns_cv_idx_sync.md](patterns_cv_idx_sync.md) — cv_idx stale reference risk when sync_needed fires during multi-step animations
- [patterns_draft_system.md](patterns_draft_system.md) — Draft pool generation pitfalls: modulo-self fill bug, duplicate timer constants
- [patterns_variable_count_migration.md](patterns_variable_count_migration.md) — Constant-to-variable migration: grep ALL refs, watch for array sizing (overflow), loop bounds, UI caps
- [patterns_network_step6.md](patterns_network_step6.md) — Network loop: incomplete multi-step command handlers, lost state across ticks, missing malloc NULL checks
- [patterns_network_protocol.md](patterns_network_protocol.md) — Protocol layer: null-term on deser, sub-struct bounds, INPUT_RELEVANT sync, stack buffers, endianness
- [patterns_lobby_auth.md](patterns_lobby_auth.md) — Lobby auth: silent getrandom failures, session cleanup on disconnect, ELO type mismatch, transaction atomicity
- [patterns_client_net_wiring.md](patterns_client_net_wiring.md) — Client networking wiring: online UI phase transition race, NET_PLAYER_VIEW_MAX_SIZE undercount, command routing split
- [patterns_async_pass_anim.md](patterns_async_pass_anim.md) — Async pass animation: server state consumption can overwrite client-side subphase during toss animations
- [patterns_deser_bounds_check.md](patterns_deser_bounds_check.md) — Deserialization bounds checks in protocol.c use hardcoded byte counts that go stale when fields are added
