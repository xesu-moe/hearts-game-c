# Architecture Reference

Single source of truth for file responsibilities and boundaries. Used by the Architecture Router subagent to route implementations to the correct file.

## Directory Structure

```
src/
├── main.c                 # Client entry point
├── core/                  # Pure game logic (no Raylib, no networking)
│   ├── card.c/h
│   ├── hand.c/h
│   ├── deck.c/h
│   ├── trick.c/h
│   ├── player.c/h
│   ├── game_state.c/h
│   ├── input_cmd.c/h      # Raylib-free command types + queue (all binaries)
│   ├── input.c/h          # Raylib input polling (client only)
│   ├── clock.c/h
│   └── settings.c/h
├── net/                   # Shared networking (client + server + lobby)
│   ├── protocol.c/h
│   ├── socket.c/h
│   ├── client_net.c/h
│   ├── cmd_send.c/h
│   ├── state_recv.c/h
│   └── reconnect.c/h
├── server/                # Game server (hh-server) — headless, no Raylib
│   ├── server_main.c
│   ├── server_game.c/h
│   ├── server_net.c/h
│   ├── room.c/h
│   └── lobby_link.c/h
├── lobby/                 # Lobby server (hh-lobby) — headless, no Raylib
│   ├── lobby_main.c
│   ├── lobby_net.c/h
│   ├── db.c/h
│   ├── auth.c/h
│   ├── rooms.c/h
│   ├── matchmaking.c/h
│   └── server_registry.c/h
├── render/                # Visual layer (client only)
│   ├── render.c/h
│   ├── anim.c/h
│   ├── easing.c/h
│   ├── layout.c/h
│   ├── card_dimens.h
│   ├── card_render.c/h
│   └── particle.c/h
├── game/                  # Game flow (client only)
│   ├── ai.c/h
│   ├── process_input.c/h
│   ├── update.c/h
│   ├── turn_flow.c/h
│   ├── play_phase.c/h
│   ├── pass_phase.c/h
│   ├── phase_transitions.c/h
│   ├── info_sync.c/h
│   ├── settings_ui.c/h
│   ├── login_ui.c/h
│   └── online_ui.c/h
├── audio/
│   └── audio.c/h
├── phase2/
│   ├── character.h
│   ├── effect.h
│   ├── contract.h
│   ├── contract_logic.c/h
│   ├── transmutation.h
│   ├── transmutation_logic.c/h
│   ├── vendetta.h
│   ├── vendetta_logic.c/h
│   ├── phase2_state.h
│   ├── phase2_defs.c/h
│   └── json_parse.c/h
└── vendor/
    └── cJSON.c/h
```

### Binary → Directory Mapping

| Binary | Links | Does NOT link |
|--------|-------|---------------|
| `hollow-hearts` (client) | `core/`, `net/`, `render/`, `game/`, `audio/`, `phase2/`, `vendor/` | `server/`, `lobby/` |
| `hh-server` (game server) | `core/`, `net/`, `server/`, `game/ai.c`, `phase2/`, `vendor/` | `render/`, `audio/`, `lobby/`, most of `game/` |
| `hh-lobby` (lobby server) | `net/`, `lobby/`, `vendor/` | `core/`, `render/`, `game/`, `audio/`, `phase2/`, `server/` |

## Per-File Responsibilities

### `src/main.c` (216 lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Entry point: window init, game loop orchestration, wiring all subsystems |
| **Owns** | `main()`, window constants, top-level game loop, subsystem init/shutdown order |
| **Does NOT contain** | Game logic, rendering logic, input processing, animation, layout math |

### `src/core/` — Pure Game Logic (no Raylib, no rendering)

#### `src/core/card.c/h` (78h + 60c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Card type definition, suit/rank enums, card identity helpers |
| **Owns** | `Card`, `Suit`, `Rank`, `CARD_NONE`, `DECK_SIZE`, `MAX_HAND_SIZE`, `NUM_PLAYERS`, `CARDS_PER_TRICK`, `card_to_index()`, `card_from_index()`, `card_is_none()`, `card_equals()`, `card_name()`, `card_points()` |
| **Does NOT contain** | Rendering, hand management, trick logic, game state |

#### `src/core/hand.c/h` (56h + 131c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Hand container: add, remove, query, sort, move cards |
| **Owns** | `Hand`, `hand_init()`, `hand_add_card()`, `hand_remove_at()`, `hand_remove_card()`, `hand_contains()`, `hand_has_suit()`, `hand_sort()`, `hand_sort_permutation()`, `hand_count_points()`, `hand_move_card()` |
| **Does NOT contain** | Deck operations, trick logic, rendering, AI logic |

#### `src/core/deck.c/h` (34h + 49c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Deck container: init, shuffle (Fisher-Yates), deal |
| **Owns** | `Deck`, `deck_init()`, `deck_shuffle()`, `deck_deal()`, `deck_deal_all()`, `deck_remaining()` |
| **Does NOT contain** | Hand logic, game rules, rendering |

#### `src/core/trick.c/h` (45h + 128c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Trick container and Hearts play-legality rules |
| **Owns** | `Trick`, `trick_init()`, `trick_play_card()`, `trick_is_complete()`, `trick_get_winner()`, `trick_count_points()`, `trick_is_valid_play()` |
| **Does NOT contain** | Game state transitions, rendering, AI decisions |

#### `src/core/player.c/h` (31h + 27c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Player struct: identity, hand, scoring |
| **Owns** | `Player`, `player_init()`, `player_new_round()`, `player_add_to_total()` |
| **Does NOT contain** | AI logic, rendering, game flow |

#### `src/core/game_state.c/h` (136h + 324c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Central game state: phases, rounds, passing, trick lifecycle, scoring |
| **Owns** | `GamePhase`, `PassDirection`, `PassSubphase`, `GameState`, `game_state_init()`, `game_state_start_game()`, `game_state_reset_to_menu()`, `game_state_new_round()`, `game_state_find_two_of_clubs()`, `game_state_select_pass()`, `game_state_all_passes_ready()`, `game_state_execute_pass()`, `game_state_current_player()`, `game_state_play_card()`, `game_state_complete_trick()`, `game_state_is_valid_play()`, `game_state_is_game_over()`, `game_state_advance_scoring()`, `game_state_get_winners()` |
| **Does NOT contain** | Rendering, AI decisions, input handling, animation |

#### `src/core/input_cmd.c/h` (Raylib-free, all binaries)

| Field | Value |
|-------|-------|
| **Responsibility** | Command types and queue, shared by client and server (no Raylib) |
| **Owns** | `InputCmdType`, `InputCmd`, `InputCmdQueue`, `INPUT_CMD_QUEUE_CAPACITY`, `input_cmd_push()`, `input_cmd_pop()`, `input_cmd_queue_empty()`, `input_cmd_queue_clear()` |
| **Does NOT contain** | Raylib input polling, action state tracking, hit-testing |

#### `src/core/input.c/h` (client only)

| Field | Value |
|-------|-------|
| **Responsibility** | Raylib input polling and button state tracking (client only) |
| **Owns** | `InputAction`, `InputState`, `input_init()`, `input_poll()`, `input_get_state()` |
| **Does NOT contain** | Command types (in input_cmd.h), hit-testing, card selection logic, game state mutation |

#### `src/core/clock.c/h` (30h + 39c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Game clock: fixed timestep accumulator, time scaling, pause |
| **Owns** | `GameClock`, `FIXED_DT`, `MAX_FRAME_DT`, `MAX_CATCHUP`, `clock_init()`, `clock_update()` |
| **Does NOT contain** | Animation timing, game logic, rendering |

#### `src/core/settings.c/h` (86h + 293c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Persistent game settings: load/save JSON, apply window/fps, name helpers |
| **Owns** | `WindowMode`, `AnimSpeed`, `AISpeed`, `Resolution`, `GameSettings`, `RESOLUTIONS[]`, `FPS_OPTIONS[]`, `settings_default()`, `settings_load()`, `settings_save()`, `settings_apply()`, `settings_anim_multiplier()`, `settings_ai_think_time()`, `settings_*_name()` |
| **Does NOT contain** | Settings UI interaction, rendering, game logic |

### `src/render/` — Visual Layer (owns all Raylib draw calls)

#### `src/render/render.c/h` (351h + 2288c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | RenderState orchestration: sync visuals with game state, update animations, draw all phases, hit-testing, drag API, card selection, chat log |
| **Owns** | `RenderState`, `DragState`, `ScoringSubphase`, `PassStagedCard`, `MenuItem`, `UIButton`, `render_init()`, `render_update()`, `render_draw()`, `render_hit_test_card()`, `render_hit_test_button()`, `render_toggle_card_selection()`, `render_clear_selection()`, `render_cancel_drag()`, `render_start_card_drag()`, `render_commit_hand_reorder()`, `render_update_snap_target()`, `render_hit_test_contract()`, `render_hit_test_transmute()`, `render_set_contract_options()`, `render_alloc_card_visual()`, `render_clear_piles()`, `render_chat_log_push()`, `render_chat_log_push_color()`, `render_effect_label()`, sync_hands(), sync_deal(), sync_buttons(), all draw_phase_*() functions |
| **Does NOT contain** | Animation math (delegates to anim.c), easing curves (delegates to easing.c), layout position calculations (delegates to layout.c), card sprite drawing (delegates to card_render.c), game logic, input polling |

#### `src/render/anim.c/h` (125h + 163c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Animation engine: start/update animations, toss bezier setup, speed control, all timing constants |
| **Owns** | `CardVisual`, `MAX_CARD_VISUALS`, `anim_start()`, `anim_update()`, `anim_toss_enabled()`, `anim_setup_toss()`, `anim_set_speed()`, `anim_get_speed()`, all `ANIM_*` duration/stagger macros, `TOSS_*` constants, `HOVER_*` constants, `ANIM_REARRANGE_BLEND_RATE` |
| **Does NOT contain** | Drawing, layout, hit-testing, game state, easing math (delegates to easing.c) |

#### `src/render/easing.c/h` (26h + 43c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Pure math: easing curves and linear interpolation |
| **Owns** | `EaseType`, `ease_apply()`, `lerpf()` |
| **Does NOT contain** | Anything Raylib, anything stateful, animation state machines |

#### `src/render/layout.c/h` (144h + 393c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Pure position/rect math: where things go on screen, scaling |
| **Owns** | `PlayerPosition`, `LayoutConfig`, `ScoringTableLayout`, `ContractsTableLayout`, `layout_hand_positions()`, `layout_trick_position()`, `layout_score_position()`, `layout_name_position()`, `layout_pass_direction_position()`, `layout_confirm_button()`, `layout_board_rect()`, `layout_board_center()`, `layout_contract_options()`, `layout_left_panel_upper()`, `layout_left_panel_lower()`, `layout_pass_staging_position()`, `layout_pile_position()`, `layout_scoring_table()`, `layout_scoring_row_y()`, `layout_scoring_card_position()`, `layout_contracts_table()`, `layout_contracts_row_y()`, `layout_recalculate()` |
| **Does NOT contain** | Drawing, animation, game state, RenderState |

#### `src/render/card_dimens.h` (header-only)

| Field | Value |
|-------|-------|
| **Responsibility** | Shared card dimension constants at 720p reference resolution |
| **Owns** | `CARD_WIDTH_REF`, `CARD_HEIGHT_REF`, `CARD_OVERLAP_REF`, `CARD_SELECT_LIFT_REF` |
| **Does NOT contain** | Logic, types, anything stateful |

#### `src/render/card_render.c/h` (52h + 281c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Drawing a single card: face/back, sprite sheet or procedural fallback |
| **Owns** | `card_render_init()`, `card_render_shutdown()`, `card_render_face()`, `card_render_back()`, `card_render_set_filter()`, `card_suit_color()`, `card_suit_symbol()`, `card_rank_string()`, sprite sheet state |
| **Does NOT contain** | Layout, animation, game logic, multiple-card orchestration |

#### `src/render/particle.c/h` (42h + 104c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Particle lifecycle: spawn burst, update physics/age, draw |
| **Owns** | `Particle`, `ParticleSystem`, `MAX_PARTICLES`, `particle_init()`, `particle_spawn_burst()`, `particle_update()`, `particle_draw()`, `particle_any_active()` |
| **Does NOT contain** | Cards, layout, game state |

### `src/game/` — Game Flow (bridges core logic and render)

#### `src/game/ai.c/h` (26h + 72c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | AI decision logic for passing and playing cards |
| **Owns** | `ai_select_pass()`, `ai_play_card()` |
| **Does NOT contain** | Input handling, game state transitions, human player logic |

#### `src/game/process_input.c/h` (24h + 343c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Translates raw mouse/key events into InputCmds via hit-testing, manages drag lifecycle (start/classify/commit/cancel) |
| **Owns** | `process_input()`, toss classification thresholds (`TOSS_CLICK_DIST`, `TOSS_MIN_SPEED`, `TOSS_DROP_RADIUS`, `TOSS_MIN_UPWARD`) |
| **Does NOT contain** | Animation math, layout calculations, direct CardVisual field manipulation, game state mutation |

#### `src/game/update.c/h` (30h + 284c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Per-frame command processing: dequeues InputCmds and dispatches phase-specific logic |
| **Owns** | `game_update()` |
| **Does NOT contain** | Rendering, drawing, input polling, animation |

#### `src/game/turn_flow.c/h` (54h + 255c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Turn/trick state machine during PHASE_PLAYING: human wait, AI think, card animate, trick display, pile collect |
| **Owns** | `FlowStep`, `TurnFlow`, `FLOW_*` timing constants, `flow_init()`, `flow_update()` |
| **Does NOT contain** | Rendering, input, card drawing |

#### `src/game/play_phase.c/h` (38h + 105c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Playing phase rules: transmutation-aware card play, player name helper |
| **Owns** | `PlayPhaseState`, `TrickTransmuteInfo` (usage), `play_card_with_transmute()`, `p2_player_name()` |
| **Does NOT contain** | Turn flow, rendering, AI logic |

#### `src/game/pass_phase.c/h` (72h + 607c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Passing phase FSM: vendetta/contract/card-pass subphases, toss/wait/receive animations, AI pass selection |
| **Owns** | `PassPhaseState`, `PASS_*_TIME` constants, `advance_pass_subphase()`, `auto_select_human_pass()`, `finalize_card_pass()`, `pass_start_toss_anim()`, `pass_toss_animations_done()`, `pass_start_receive_anim()`, `pass_receive_animations_done()`, `pass_subphase_update()`, `setup_contract_ui()`, `setup_vendetta_ui()`, `pass_subphase_time_limit()` |
| **Does NOT contain** | Card drawing, turn flow, score calculation |

#### `src/game/phase_transitions.c/h` (37h + 187c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Phase entry/exit orchestration: scoring animation setup, deal-to-pass/play transition, particle bursts, chat log entries |
| **Owns** | `phase_transition_update()`, `phase_transition_post_render()` |
| **Does NOT contain** | Per-frame game updates, input processing, drawing |

#### `src/game/info_sync.c/h` (26h + 188c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Syncs game state into RenderState display fields: info panel, playability flags, vendetta options, transmutation UI |
| **Owns** | `info_sync_update()`, `info_sync_playability()` |
| **Does NOT contain** | Game logic decisions, drawing, input |

#### `src/game/settings_ui.c/h` (31h + 130c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Settings screen logic: value display sync, setting adjustment, deferred display apply |
| **Owns** | `SettingsUIState`, `sync_settings_values()`, `setting_adjust()`, `apply_display_settings()` |
| **Does NOT contain** | Drawing, game logic, input polling |

#### `src/game/login_ui.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Login/register screen logic: text input handling, auth request, error display |
| **Owns** | `LoginUIState`, login/register form logic, token storage |
| **Does NOT contain** | Networking I/O (uses `net/client_net`), drawing, game logic |

#### `src/game/online_ui.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Online menu logic: create room, join by code, matchmaking queue, waiting room |
| **Owns** | `OnlineUIState`, room creation flow, matchmaking queue state, waiting room player list |
| **Does NOT contain** | Networking I/O (uses `net/client_net`), drawing, game logic |

### `src/net/` — Shared Networking (client + server + lobby)

#### `src/net/protocol.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Message type definitions, binary serialization/deserialization, length-prefix framing format |
| **Owns** | `NetMsgType` enum, `NetMsg` tagged union, `net_msg_serialize()`, `net_msg_deserialize()`, `net_msg_size()`, framing constants |
| **Does NOT contain** | Socket I/O, game logic, rendering, protocol interpretation |

#### `src/net/socket.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Non-blocking TCP socket wrappers, poll-based multiplexing, connection state machine |
| **Owns** | `NetSocket`, `NetConnState`, `net_listen()`, `net_accept()`, `net_connect()`, `net_send()`, `net_recv()`, `net_poll()`, `net_close()`, per-connection send/recv ring buffers |
| **Does NOT contain** | Message semantics, game state, protocol interpretation, game logic |

#### `src/net/client_net.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Client-side connection lifecycle: connect, handshake, per-frame send/recv, connection state |
| **Owns** | `ClientNet`, `client_net_init()`, `client_net_connect()`, `client_net_update()`, `client_net_disconnect()`, `client_net_state()`, `client_net_send()`, `client_net_recv()` |
| **Does NOT contain** | Server logic, rendering, game rules, command serialization (delegates to `cmd_send`) |

#### `src/net/cmd_send.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Serialize `InputCmd` to network message and send to server, filter out client-only commands |
| **Owns** | `cmd_send()`, `cmd_is_network_relevant()` |
| **Does NOT contain** | Command processing, game state mutation, rendering, socket management |

#### `src/net/state_recv.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Receive and apply server state updates to local `GameState`, trigger render sync |
| **Owns** | `state_recv_apply()`, state snapshot deserialization, hidden-information handling |
| **Does NOT contain** | Rendering, command generation, server logic, socket management |

#### `src/net/reconnect.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Reconnection logic: exponential backoff, state resync after reconnect |
| **Owns** | `ReconnectState`, `reconnect_start()`, `reconnect_update()`, `reconnect_reset()`, backoff timing |
| **Does NOT contain** | Socket management (uses `client_net`), game logic, rendering |

### `src/server/` — Game Server (headless, no Raylib)

#### `src/server/server_main.c` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Server entry point: headless fixed-timestep game loop, signal handling, shutdown |
| **Owns** | `main()` (server), server tick loop, subsystem init/shutdown |
| **Does NOT contain** | Raylib, rendering, client UI, game logic (delegates to `server_game`) |

#### `src/server/server_game.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Server-side game tick: validate incoming `InputCmd`, apply to room's `GameState`, build per-player state updates (filtering hidden information) |
| **Owns** | `server_game_tick()`, `server_validate_cmd()`, `server_build_state_update()`, per-player state filtering |
| **Does NOT contain** | Socket I/O (uses `net/`), room lifecycle, rendering |

#### `src/server/server_net.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Server network loop: accept connections, authenticate tokens, route messages to rooms, broadcast state updates |
| **Owns** | `server_net_init()`, `server_net_update()`, `server_net_broadcast()`, connection→room mapping, token verification |
| **Does NOT contain** | Game logic, room lifecycle (uses `room`), protocol format (uses `net/protocol`) |

#### `src/server/room.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Room management: create/join/leave, player slot tracking, disconnect timers, AI fallback |
| **Owns** | `Room`, `RoomSlot`, `RoomStatus`, `room_create()`, `room_join()`, `room_leave()`, `room_tick()`, `room_disconnect_player()`, `room_reconnect_player()`, room code generation |
| **Does NOT contain** | Networking (receives commands via function calls), protocol details, rendering |

#### `src/server/lobby_link.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Communication with lobby server: register on startup, report game results, heartbeat |
| **Owns** | `lobby_link_init()`, `lobby_link_register()`, `lobby_link_report_result()`, `lobby_link_heartbeat()` |
| **Does NOT contain** | Game logic, room management, client connections |

### `src/lobby/` — Lobby Server (headless, no Raylib, no game logic)

#### `src/lobby/lobby_main.c` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Lobby entry point: event loop, subsystem init/shutdown |
| **Owns** | `main()` (lobby), lobby tick loop |
| **Does NOT contain** | Game logic, rendering, game state |

#### `src/lobby/lobby_net.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Lobby network loop: accept client connections, route lobby messages (login, room requests, matchmaking) |
| **Owns** | `lobby_net_init()`, `lobby_net_update()`, message routing |
| **Does NOT contain** | Authentication logic (delegates to `auth`), room management (delegates to `rooms`), database |

#### `src/lobby/db.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | SQLite connection management, schema migrations, parameterized query helpers |
| **Owns** | `LobbyDB`, `db_init()`, `db_close()`, `db_migrate()`, `db_exec()`, `db_query()` |
| **Does NOT contain** | Business logic, authentication, networking, game logic |

#### `src/lobby/auth.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Account system: registration, login, password hashing, session token management |
| **Owns** | `auth_register()`, `auth_login()`, `auth_validate_token()`, `auth_logout()`, password hashing helpers |
| **Does NOT contain** | Database schema (uses `db`), networking, game logic |

#### `src/lobby/rooms.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Room code generation, code→game server mapping, expiration tracking |
| **Owns** | `lobby_room_create()`, `lobby_room_resolve()`, `lobby_room_expire()`, code generation (ambiguity-safe charset) |
| **Does NOT contain** | Game logic, matchmaking, authentication, database schema |

#### `src/lobby/matchmaking.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Matchmaking queue: player grouping, FIFO (future: ELO-based), room creation triggers |
| **Owns** | `MatchQueue`, `matchmaking_enqueue()`, `matchmaking_dequeue()`, `matchmaking_tick()`, `matchmaking_cancel()` |
| **Does NOT contain** | Authentication, database, networking, game logic |

#### `src/lobby/server_registry.c/h` (new — Phase 3)

| Field | Value |
|-------|-------|
| **Responsibility** | Track active game servers: registration, health checks, load-based selection |
| **Owns** | `ServerEntry`, `registry_add()`, `registry_remove()`, `registry_select_server()`, `registry_heartbeat()` |
| **Does NOT contain** | Game logic, authentication, room codes, matchmaking |

### `src/audio/`

#### `src/audio/audio.c/h` (76h + 201c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Sound effects and music: init, play, crossfade, stagger sequences, volume control |
| **Owns** | `MusicContext`, `SfxId`, `SfxStagger`, `AudioState`, `audio_init()`, `audio_shutdown()`, `audio_update()`, `audio_set_music()`, `audio_play_sfx()`, `audio_apply_settings()`, `audio_start_stagger()` |
| **Does NOT contain** | Game logic, rendering, input |

### `src/phase2/` — Hollow Hearts Modification Systems

#### `src/phase2/effect.h` (64 lines, header-only)

| Field | Value |
|-------|-------|
| **Responsibility** | Shared effect types used by contracts, vendettas, and active effects |
| **Owns** | `EffectType`, `Effect`, `EffectScope`, `ActiveEffect`, `MAX_ACTIVE_EFFECTS` |
| **Does NOT contain** | Logic, rendering, game state |

#### `src/phase2/contract.h` (82 lines, header-only)

| Field | Value |
|-------|-------|
| **Responsibility** | Contract type definitions: conditions, rewards, instances |
| **Owns** | `ConditionType`, `ConditionParam`, `ContractDef`, `ContractInstance`, `MAX_CONTRACT_DEFS`, `MAX_CONTRACT_REWARD`, `MAX_CONTRACT_TRANSMUTE_REWARD`, `CONTRACT_TIERS` |
| **Does NOT contain** | Logic, rendering, game state management |

#### `src/phase2/contract_logic.c/h` (55h + 295c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Contract system logic: init, reset, select, evaluate, apply rewards, AI selection |
| **Owns** | `contract_state_init()`, `contract_round_reset()`, `contract_get_available()`, `contract_select()`, `contract_all_chosen()`, `contract_on_trick_complete()`, `contract_evaluate()`, `contract_apply_reward()`, `contract_ai_select()`, `MAX_CONTRACT_OPTIONS` |
| **Does NOT contain** | Rendering, input, vendetta logic, transmutation logic |

#### `src/phase2/transmutation.h` (81 lines, header-only)

| Field | Value |
|-------|-------|
| **Responsibility** | Transmutation type definitions: special properties, inventory, hand/trick tracking |
| **Owns** | `TransmuteSpecial`, `SuitMask`, `TransmutationDef`, `TransmuteInventory`, `TransmuteSlot`, `HandTransmuteState`, `TrickTransmuteInfo`, `MAX_TRANSMUTATION_DEFS`, `MAX_TRANSMUTE_INVENTORY` |
| **Does NOT contain** | Logic, rendering |

#### `src/phase2/transmutation_logic.c/h` (75h + 416c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Transmutation system logic: inventory management, hand application, trick resolution overrides, validity checks, AI |
| **Owns** | `transmute_inv_init/add/remove()`, `transmute_hand_init()`, `transmute_apply()`, `transmute_hand_remove_at()`, `transmute_hand_sort_sync()`, `transmute_is_transmuted()`, `transmute_get_def()`, `transmute_get_original()`, `transmute_card_points()`, `transmute_can_follow_suit()`, `transmute_is_always_win/lose()`, `transmute_trick_get_winner()`, `transmute_trick_count_points()`, `transmute_is_valid_play()`, `transmute_ai_apply()` |
| **Does NOT contain** | Rendering, contract logic, vendetta logic |

#### `src/phase2/vendetta.h` (41 lines, header-only)

| Field | Value |
|-------|-------|
| **Responsibility** | Vendetta type definitions: timing, effects |
| **Owns** | `VendettaTiming`, `VendettaDef`, `MAX_VENDETTA_DEFS`, `MAX_VENDETTA_EFFECTS`, `VENDETTA_DISPLAY_NAME` |
| **Does NOT contain** | Logic, rendering |

#### `src/phase2/vendetta_logic.c/h` (53h + 152c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Vendetta system logic: determine player, get options, select, apply effects, AI activation |
| **Owns** | `vendetta_round_reset()`, `vendetta_determine_player()`, `vendetta_get_available()`, `vendetta_select()`, `vendetta_apply()`, `vendetta_ai_activate()`, `vendetta_has_options()`, `MAX_VENDETTA_OPTIONS` |
| **Does NOT contain** | Rendering, contract logic, transmutation logic |

#### `src/phase2/character.h` (51 lines, header-only)

| Field | Value |
|-------|-------|
| **Responsibility** | Character type definitions: figure types, mechanics union (King contracts, Queen vendettas, Jack reserved) |
| **Owns** | `FigureType`, `CharacterDef`, `MAX_CHARACTER_DEFS`, `MAX_CHAR_VENDETTAS` |
| **Does NOT contain** | Logic, rendering |

#### `src/phase2/phase2_state.h` (80 lines, header-only)

| Field | Value |
|-------|-------|
| **Responsibility** | Phase 2 aggregate state: per-player and per-round mutable state |
| **Owns** | `KingProgress`, `PlayerPhase2`, `RoundPhase2`, `Phase2State` |
| **Does NOT contain** | Logic, rendering, type definitions (delegates to contract.h, effect.h, transmutation.h) |

#### `src/phase2/phase2_defs.c/h` (45h + 105c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | Global definition tables and lookup functions for all Phase 2 types |
| **Owns** | `g_contract_defs[]`, `g_vendetta_defs[]`, `g_transmutation_defs[]`, `g_character_defs[]`, counts, `phase2_defs_init()`, `phase2_get_contract/vendetta/transmutation/character()` |
| **Does NOT contain** | Game logic, rendering, parsing logic (delegates to json_parse.c) |

#### `src/phase2/json_parse.c/h` (50h + 560c lines)

| Field | Value |
|-------|-------|
| **Responsibility** | JSON file parsing for all Phase 2 definition types |
| **Owns** | `EnumMapping`, `enum_from_string()`, `json_load_contracts()`, `json_load_vendettas()`, `json_load_transmutations()`, `json_load_characters()`, all enum mapping tables |
| **Does NOT contain** | Game logic, rendering, definition storage (writes into arrays owned by phase2_defs.c) |

### `src/vendor/`

#### `src/vendor/cJSON.c/h`

| Field | Value |
|-------|-------|
| **Responsibility** | Third-party JSON parsing library |
| **Owns** | All cJSON types and functions |
| **Does NOT contain** | Game-specific code (do not modify) |

## Shared Headers

| Header | Types/Macros Exposed | Used By |
|--------|---------------------|---------|
| `core/card.h` | `Card`, `Suit`, `Rank`, `DECK_SIZE`, `MAX_HAND_SIZE`, `NUM_PLAYERS`, `CARDS_PER_TRICK`, `CARD_NONE` | Nearly every file in the project |
| `core/game_state.h` | `GamePhase`, `PassDirection`, `PassSubphase`, `GameState`, `PASS_CARD_COUNT`, `GAME_OVER_SCORE` | main.c, all game/ files, render.h, ai.h, phase2 logic files, server/server_game, net/state_recv |
| `core/input_cmd.h` | `InputCmdType`, `InputCmd`, `InputCmdQueue`, `INPUT_CMD_QUEUE_CAPACITY` | All binaries: protocol.c, server_game, update.c, process_input.c, main.c |
| `core/input.h` | `InputAction`, `InputState` (re-exports input_cmd.h) | Client only: main.c, process_input.c, input.c |
| `net/protocol.h` | `NetMsgType`, `NetMsg`, serialization functions, framing constants | All net/ files, server/ files, lobby/ files |
| `net/socket.h` | `NetSocket`, `NetConnState`, TCP wrapper functions | net/client_net, server/server_net, lobby/lobby_net |
| `net/client_net.h` | `ClientNet`, client connection lifecycle | main.c, game/login_ui, game/online_ui, net/cmd_send, net/state_recv |
| `render/render.h` | `RenderState`, `DragState`, `UIButton`, `CardVisual` (via anim.h), `ScoringSubphase`, `PassStagedCard` | main.c, all game/ files (client only) |
| `render/anim.h` | `CardVisual`, `MAX_CARD_VISUALS`, all `ANIM_*` timing constants | render.c, turn_flow.c, pass_phase.c, phase_transitions.c, main.c |
| `render/layout.h` | `LayoutConfig`, `PlayerPosition`, `ScoringTableLayout` | render.c, layout.c, process_input.c, pass_phase.c, turn_flow.c, phase_transitions.c |
| `render/card_dimens.h` | `CARD_WIDTH_REF`, `CARD_HEIGHT_REF`, `CARD_OVERLAP_REF`, `CARD_SELECT_LIFT_REF` | render.h (transitive), layout.c, card_render.c |
| `phase2/phase2_state.h` | `Phase2State`, `PlayerPhase2`, `RoundPhase2`, `KingProgress` | All phase2 logic files, all game/ files, ai.h, main.c, server/server_game |
| `phase2/effect.h` | `EffectType`, `Effect`, `EffectScope`, `ActiveEffect` | contract.h, vendetta.h, phase2_state.h, render.h |
| `phase2/transmutation.h` | `TransmutationDef`, `TransmuteInventory`, `HandTransmuteState`, `TrickTransmuteInfo` | phase2_state.h, transmutation_logic.h, play_phase.h, json_parse.h |
| `core/settings.h` | `GameSettings`, `WindowMode`, `AnimSpeed`, `AISpeed` | main.c, settings.c, turn_flow.h, update.h, settings_ui.h, audio.h |
| `server/room.h` | `Room`, `RoomSlot`, `RoomStatus` | server/server_main, server/server_net, server/server_game, server/lobby_link |
| `lobby/db.h` | `LobbyDB`, query helpers | lobby/auth, lobby/rooms, lobby/matchmaking, lobby/server_registry |

## Dependency Graph (project includes only)

### Client (`hollow-hearts`)

| Source File | Includes |
|-------------|----------|
| `main.c` | core/{clock,game_state,input,settings}.h, render/{anim,card_render,render}.h, phase2/{phase2_defs,contract_logic,transmutation_logic}.h, audio/audio.h, game/{ai,play_phase,pass_phase,turn_flow,process_input,update,settings_ui,info_sync,phase_transitions,login_ui,online_ui}.h, net/client_net.h |
| `core/card.c` | card.h |
| `core/hand.c` | hand.h |
| `core/deck.c` | deck.h |
| `core/trick.c` | trick.h |
| `core/player.c` | player.h |
| `core/game_state.c` | game_state.h |
| `core/input.c` | input.h |
| `core/clock.c` | clock.h |
| `core/settings.c` | settings.h, vendor/cJSON.h |
| `render/render.c` | render.h, card_render.h, core/hand.h, phase2/phase2_state.h, phase2/vendetta.h |
| `render/anim.c` | anim.h |
| `render/easing.c` | easing.h |
| `render/layout.c` | layout.h, card_dimens.h |
| `render/card_render.c` | card_render.h, card_dimens.h |
| `render/particle.c` | particle.h |
| `game/ai.c` | ai.h, core/hand.h, render/render.h, phase2/transmutation_logic.h |
| `game/process_input.c` | process_input.h, core/input.h, render/{render,layout}.h |
| `game/update.c` | update.h, core/input.h, render/render.h, phase2/{contract_logic,vendetta_logic,transmutation_logic,phase2_defs}.h |
| `game/turn_flow.c` | turn_flow.h, ai.h, core/trick.h, render/{render,layout}.h, phase2/{contract_logic,vendetta_logic,transmutation_logic,phase2_defs}.h |
| `game/play_phase.c` | play_phase.h, core/{hand,trick}.h, render/render.h, phase2/{transmutation_logic,phase2_defs}.h |
| `game/pass_phase.c` | pass_phase.h, ai.h, core/hand.h, render/{anim,layout,render}.h, phase2/{contract_logic,vendetta_logic,transmutation_logic,phase2_defs}.h |
| `game/phase_transitions.c` | phase_transitions.h, core/card.h, render/{render,anim,particle}.h, phase2/{contract_logic,vendetta_logic}.h |
| `game/info_sync.c` | info_sync.h, render/render.h, phase2/{phase2_defs,vendetta_logic,transmutation_logic}.h |
| `game/settings_ui.c` | settings_ui.h, render/render.h |
| `game/login_ui.c` | login_ui.h, net/client_net.h, render/render.h |
| `game/online_ui.c` | online_ui.h, net/client_net.h, render/render.h |
| `audio/audio.c` | audio/audio.h, core/settings.h |
| `net/protocol.c` | protocol.h, core/input.h, core/game_state.h |
| `net/socket.c` | socket.h |
| `net/client_net.c` | client_net.h, net/{protocol,socket}.h |
| `net/cmd_send.c` | cmd_send.h, net/{protocol,client_net}.h, core/input.h |
| `net/state_recv.c` | state_recv.h, net/protocol.h, core/game_state.h |
| `net/reconnect.c` | reconnect.h, net/client_net.h |
| `phase2/contract_logic.c` | contract_logic.h, phase2_defs.h, transmutation_logic.h, core/card.h |
| `phase2/transmutation_logic.c` | transmutation_logic.h, phase2_defs.h |
| `phase2/vendetta_logic.c` | vendetta_logic.h, phase2_defs.h |
| `phase2/phase2_defs.c` | phase2_defs.h, json_parse.h |
| `phase2/json_parse.c` | json_parse.h, vendor/cJSON.h |

### Game Server (`hh-server`)

| Source File | Includes |
|-------------|----------|
| `server/server_main.c` | server/{server_game,server_net,room,lobby_link}.h, core/{clock,game_state}.h, net/protocol.h |
| `server/server_game.c` | server_game.h, server/room.h, core/{game_state,input,trick}.h, net/protocol.h, game/ai.h, phase2/{phase2_state,contract_logic,transmutation_logic,phase2_defs}.h |
| `server/server_net.c` | server_net.h, server/room.h, net/{protocol,socket}.h |
| `server/room.c` | room.h, core/game_state.h, game/ai.h |
| `server/lobby_link.c` | lobby_link.h, net/{protocol,socket}.h |

### Lobby Server (`hh-lobby`)

| Source File | Includes |
|-------------|----------|
| `lobby/lobby_main.c` | lobby/{lobby_net,db,auth,rooms,matchmaking,server_registry}.h |
| `lobby/lobby_net.c` | lobby_net.h, net/{protocol,socket}.h |
| `lobby/db.c` | db.h (+ SQLite headers) |
| `lobby/auth.c` | auth.h, lobby/db.h |
| `lobby/rooms.c` | rooms.h, lobby/{db,server_registry}.h |
| `lobby/matchmaking.c` | matchmaking.h, lobby/{rooms,server_registry}.h |
| `lobby/server_registry.c` | server_registry.h, lobby/db.h, net/{protocol,socket}.h |

## Rules

1. **Each .c file has exactly one responsibility.** If a function doesn't match the file's stated purpose, it belongs elsewhere.

2. **Game logic never goes in rendering files.** `render/` files must not include `game_state.h` mutably or call functions that change `GameState`. They read game state immutably for display.

3. **Rendering never goes in logic files.** `core/` and `phase2/` files must not include Raylib headers or render types. Exception: `core/deck.c` uses `GetRandomValue()` and `core/settings.c` uses Raylib window management.

4. **Pure math/utility functions have no side effects.** `easing.c` and `layout.c` (position math) are stateless. They must not access game state, render state, or call Raylib drawing functions. `layout.c` includes `card_dimens.h` for `CARD_WIDTH_REF`/`CARD_HEIGHT_REF` constants — a lightweight, leaf-level header with no further dependencies.

5. **Animation files manage state machines and transitions, but do not draw.** `anim.c` updates `CardVisual` positions; `render.c` calls `card_render.c` to actually draw them.

6. **All shared types go in dedicated header-only files.** `Card` in `card.h`, `Effect` in `effect.h`, `TransmutationDef` in `transmutation.h`, etc. Type definitions are never scattered across .c files.

7. **When a file exceeds ~400 lines, it is a candidate for splitting.** See Proposed / TODO section.

8. **game/ files read/write GameState and call render/ public API.** They never manipulate `CardVisual` fields directly or compute layout positions. Exception: `process_input.c` uses `layout_trick_position()` for toss classification (geometry query, not drawing).

9. **render.c orchestrates but delegates.** It calls `layout_*()` for positions, `anim_*()` for animation, `card_render_*()` for drawing. New position calculations go in `layout.c`, new animation constants in `anim.h`, new easing in `easing.c`.

10. **core/ is pure game logic.** No Raylib includes (except `deck.c` for `GetRandomValue` and `settings.c` for window management), no render types, no visual concepts. If a function needs screen positions, it does not belong in `core/`.

11. **net/ is transport only.** `net/` files handle serialization, framing, and connection lifecycle. They never make game logic decisions, validate moves, or access render state. `protocol.c` defines the wire format; `socket.c` handles TCP I/O; other files are role-specific wrappers.

12. **server/ is authoritative and headless.** `server/` files never include Raylib headers, render types, or audio. The server owns all game state truth — it validates every `InputCmd` before applying. `server_game.c` filters hidden information (opponents' cards) from state updates before broadcast.

13. **lobby/ has no game logic.** `lobby/` files never include `game_state.h`, `core/` headers, or `phase2/` headers. The lobby manages player identity (accounts, tokens), game discovery (room codes, matchmaking), and server registry. It communicates with game servers via `net/protocol` messages only.

14. **Anti-cheat: server never sends hidden card identities.** State updates to clients include: own hand (full cards), opponents' hands (card count only), current trick (face-up cards), scores, phase, turn. This is enforced in `server_game.c` when building per-player state updates. Violating this rule breaks the core anti-cheat guarantee.

15. **Binary isolation.** `render/`, `audio/`, most of `game/` are client-only — they must never be linked into `hh-server` or `hh-lobby`. `lobby/` is lobby-only — never linked into `hh-server` or `hollow-hearts`. `server/` is server-only — never linked into `hollow-hearts` or `hh-lobby`. See Binary → Directory Mapping table.

## Proposed / TODO

### Files exceeding 400 lines (splitting candidates)

| File | Lines | Issue |
|------|-------|-------|
| `render/render.c` | **2288** | Massively oversized. Contains sync logic, animation updates, hover/drag math, ALL phase draw functions, hit-testing, drag API, chat log, scoring animation, and text wrapping. Should be split into at minimum: `render_sync.c` (sync_hands, sync_deal, sync_buttons), `render_draw.c` (all draw_phase_* functions), and keep `render.c` as the orchestrator (render_init/update/draw + public API). The text_wrapped helper and chat log could also be their own file. |
| `game/pass_phase.c` | **607** | Contains both pass subphase FSM logic and the toss/receive animation setup. The animation setup functions (`pass_start_toss_anim`, `pass_start_receive_anim`) are tightly coupled to render state — consider whether they belong in `render/` or a dedicated `game/pass_anim.c`. |
| `phase2/json_parse.c` | **560** | Each loader is independent. Could split into per-type files if it grows further, but the current size is manageable with clear section separators. |
| `phase2/transmutation_logic.c` | **416** | Near the threshold. Query, trick resolution, and validity functions are distinct groups. Manageable for now. |

### Mixed concerns observed

| File | Issue |
|------|-------|
| `core/deck.c` | Uses `GetRandomValue()` from Raylib — violates the "no Raylib in core/" rule. Should accept an RNG function pointer or use C stdlib `rand()` instead. |
| `core/settings.c` | Uses Raylib for window management (`ToggleFullscreen`, `SetWindowSize`, etc.) and file I/O. The `settings_apply()` function reaches deeply into Raylib — consider whether this belongs in a `game/` or `render/` file instead. |
| `game/pass_phase.c` | `finalize_card_pass()` duplicates the transmutation save/restore pattern with `pass_start_receive_anim()`. The duplication is a code smell — `finalize_card_pass` appears to be a legacy function that may be dead code after the toss animation was added. |

#### Resolved (2026-03-20)

- `render/layout.c` and `render/card_render.c` no longer include `render.h` — now use `card_dimens.h` for shared constants.
- `audio/audio.c` no longer includes `render/anim.h` — `audio_update()` receives `anim_speed` as a parameter.
- `core/ai.c/h` moved to `game/ai.c/h` — AI depends on RenderState and play_phase, which belong in the game layer.
