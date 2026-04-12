---
name: Lobby auth and session patterns
description: Common issues in lobby server auth/session code -- silent getrandom failures, session cleanup on disconnect, ELO type mismatch
type: project
---

Key patterns found in lobby server audit (2026-03-26):

1. **getrandom failure propagation**: `auth_generate_challenge` and `auth_random_bytes` calls for session tokens silently ignore failures. Always check return values from crypto-random functions.

2. **Session token cleanup on disconnect**: `lby_cleanup_connection` does not call `auth_logout` for authenticated clients, leaving tokens valid for 24h TTL. Watch for this pattern in any connection-cleanup code.

3. **ELO type mismatch**: DB stores `elo_rating` as `REAL` but `auth.c` reads it via `sqlite3_column_int` (truncates). `lobby_net.c` reads via `sqlite3_column_double` (correct). Inconsistent access patterns for the same column.

4. **Transaction error handling**: `lby_handle_server_result` uses raw `sqlite3_exec("BEGIN")` with a complex multi-step transaction. Failures inside the loop continue without rollback, risking partial commits.

**Why:** These are recurring patterns in C server code with SQLite -- worth checking in future lobby/server audits.
**How to apply:** On any future lobby code review, check getrandom propagation, disconnect cleanup paths, and transaction atomicity.
