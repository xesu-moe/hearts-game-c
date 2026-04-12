---
name: Network Protocol Audit Patterns
description: Recurring issues found in net/ layer: null-termination on deser, bounds in sub-struct deser, INPUT_RELEVANT sync, stack buffers
type: project
---

## Null-termination in deserialization
When `read_bytes` copies a fixed-size char buffer from wire, the deserializer MUST add explicit null-termination. `deser_handshake` does this correctly; `deser_error` and `deser_chat` did not. Watch for this in any new message types with string fields.

## Sub-struct deserialization bounds
`deser_player_view` delegates to sub-deserializers (e.g., `deser_contract_view`) that do NOT receive or check buffer length. A crafted short payload can cause reads past end of linearized buffer. A cursor pattern (buf + len + error flag) would fix globally.

## INPUT_RELEVANT array sync
The `INPUT_RELEVANT[]` designated-initializer array in protocol.c must be updated whenever new `InputCmdType` enum values are added. Currently stops at `INPUT_CMD_RETURN_TO_MENU` but enum goes to `INPUT_CMD_OPEN_STATS` (8 missing entries from Steps 19-21). The bounds check saves it, but it's fragile. A `_Static_assert` would catch this at compile time.

## server_game_apply_cmd completeness
New network-relevant InputCmdTypes need corresponding case handlers in server_game_apply_cmd(). Missing: INPUT_CMD_SELECT_TRANSMUTATION and INPUT_CMD_APPLY_TRANSMUTATION (marked relevant in INPUT_RELEVANT but no server handler). They fall to default: and reject, meaning transmutation is broken in online games.

## Stack buffer sizes in send/recv paths
`net_socket_send_msg` and `net_socket_recv_msg` each allocate ~8KB on the stack. `NetMsg` on the stack adds ~2-4KB. Single-threaded use makes `static` buffers safe and preferable.

## Endianness
The protocol claims "little-endian" but uses `memcpy` (host-native). Safe for x86-64 only. Document this limitation.
