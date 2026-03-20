# Hollow Hearts

A deck-building Hearts modification, built in C with Raylib. Players select designed characters as the figures in their deck, unlocking unique Contracts, Vendettas, and Transmutation Cards.

## Game Vision & Roadmap

### Phase 1: Vanilla Hearts (current)

Build a fully working standard Hearts game first. All engine systems must be designed with Phase 2 in mind, but no modification mechanics are implemented until vanilla Hearts is complete and stable.

### Phase 2: Hollow Hearts Modifications

Once vanilla Hearts is done, integrate the following systems:

#### Characters (Deck Building)

Players select designed characters to be the figures (Jack, Queen, King) in their deck. Each character has unique art and determines which Contracts and Vendettas are available to the player. Character selection happens before the game starts.

#### Contracts (Per-Round Secret Missions)

- Each round, players secretly choose a Contract (e.g., "Don't score any heart", "Obtain 4 club cards")
- Contracts are hidden from other players unless revealed
- Completing a Contract grants a persistent benefit (e.g., "Score -1 heart every round") and/or Transmutation Cards
- Available Contracts depend on the player's chosen character

#### Transmutation Cards

- Earned as rewards from completed Contracts (stored in a persistent per-player inventory)
- During the card pass phase, a player can transmute a hand card into a special card (e.g., an always-win joker, a duplicate Queen of Spades)
- Transmuted cards can be kept for personal use or passed to opponents as poison (negative transmutations)
- Special properties: ALWAYS_WIN (beats everything), ALWAYS_LOSE (loses to everything), multi-suit masks (can follow multiple suits)
- Custom point values override normal card scoring
- Definitions loaded from `assets/defs/transmutations.json`

#### Vendetta

- The player who scored the most points in the previous round gets a special action during the current round
- Actions include switching passing directions, revealing a player's contract, etc.
- Vendetta options come from the player's Queen characters and can trigger during passing or playing phase

### Engine Design Principle

**Build vanilla, architect for mods.** Every system (input, game state, scoring, phases) must have clear extension points for Phase 2 mechanics. Use the Command Pattern for actions, keep game rules data-driven where possible, and ensure the phase FSM can accommodate new phases (character select, contract pick, vendetta, transmutation) without rewriting the core loop.

## Build

```sh
make          # release build
make debug    # debug build (-DDEBUG -g -O0, enables debug cheats)
```

## Tech

- Language: C (C11)
- Graphics: Raylib
- Build: Make

## Conventions

- Format with `clang-format` (project `.clang-format` when added)
- Prefer simple, readable C over clever tricks
- Naming: `snake_case` for functions/variables, `PascalCase` for types, `UPPER_CASE` for constants/macros

## Development Workflow

Every feature or significant change follows a 5-step pipeline using subagents. The main agent orchestrates the workflow and writes all implementation code.

### Step 1: Plan — `@game-developer`

Before implementing any new system, feature, or multi-file change, consult `@game-developer`. This agent:
- Analyzes the request against the current codebase
- Reads architecture docs from `.claude/docs/`
- Produces a structured plan with types, function signatures, integration points, and a checklist

**When to use:** New features, new game systems, complex changes touching 2+ files, architectural decisions.
**When to skip:** Trivial bug fixes, single-line changes, formatting, comments.

### Step 2: Route — `@architecture-router`

Immediately after `@game-developer` produces a plan and before the main agent begins writing, run `@architecture-router`. This agent:
- Reads `ARCHITECTURE.md` and the file tree to determine where every planned item (function, struct, enum) belongs
- Produces a Routing Table mapping each item to a target file with a rationale
- Proposes new files if no existing file fits, and flags mixed-concern or bloat risks
- Does NOT write or modify code — it only decides where code belongs

The main agent **must** receive the Routing Table as a constraint before implementing.

**When to use:** Whenever `@game-developer` produces a plan (i.e., new features, new systems, multi-file changes).
**When to skip:** Same as `@game-developer` — trivial bug fixes, single-line changes, formatting, comments.

### Step 3: Implement — Main Agent

The main agent executes the plan from `@game-developer`, constrained by the Routing Table from `@architecture-router`:
- Follow the checklist item by item
- Create/modify files as specified in the plan
- Leverage auto-triggered C skills (`c-data-structures`, `c-memory-management`, `c-systems-programming`) for correct patterns — write it right the first time to minimize audit rework
- Use **Context7** MCP server to verify Raylib API signatures before calling them
- Build with `make` to verify compilation

### Step 4: Map Dependencies — `@dependency-mapper`

After implementation, run `@dependency-mapper` to update the dependency graph. This agent:
- Updates `.claude/deps.json` with new/changed dependencies
- Adds/updates in-file dependency headers in `.h` and `.c` files
- Emits an impact alert if the change could break other files

**When to use:** Any change to headers, structs, enums, typedefs, function signatures, or file creation/deletion.
**When to skip:** Implementation-only changes inside function bodies, comments, formatting.

### Step 5: Review — `@code-auditor`

After implementation (and dependency mapping if applicable), run `@code-auditor` to review. This agent checks:
- Correctness, memory safety, performance
- C best practices, naming conventions
- Raylib-specific patterns

Fix any critical/warning issues before considering the task complete.

### Workflow Summary

```
Feature Request
    │
    ▼
@game-developer       →  Structured Plan
    │
    ▼
@architecture-router  →  Routing Table (file assignments)
    │
    ▼
Main Agent            →  Implementation + `make`
    │
    ▼
@dependency-mapper    →  deps.json + impact report (if headers/types changed)
    │
    ▼
@code-auditor         →  Audit report → fix issues → done
```

## Project Structure

```
hollow-hearts/
├── CLAUDE.md              # This file
├── Makefile               # Build system
├── src/
│   ├── main.c             # Entry point, top-level loop wiring
│   ├── core/              # Pure game logic — no Raylib, no rendering
│   │   ├── card.c/h       # Card type, suit/rank helpers, point values
│   │   ├── hand.c/h       # Hand container (add, remove, sort, move, query)
│   │   ├── deck.c/h       # Deck (shuffle, deal)
│   │   ├── trick.c/h      # Trick (play card, determine winner)
│   │   ├── player.c/h     # Player struct
│   │   ├── game_state.c/h # GameState, phase enum, round/trick state
│   │   ├── input.c/h      # Input abstraction (poll, command queue)
│   │   ├── clock.c/h      # Game clock / time scaling
│   │   └── settings.c/h   # Persistent game settings
│   ├── render/            # Everything visual — owns Raylib calls
│   │   ├── render.c/h     # RenderState, sync, update, draw, hit-testing, drag API
│   │   ├── anim.c/h       # Animation engine (start, update, toss, bezier, timing constants)
│   │   ├── easing.c/h     # Easing math (ease_apply, lerpf)
│   │   ├── layout.c/h     # Layout math (positions, rects, scaling — no drawing)
│   │   ├── card_dimens.h  # Shared card dimension constants (CARD_WIDTH_REF, etc.)
│   │   ├── card_render.c/h # Card sprite drawing (face, back, procedural fallback)
│   │   └── particle.c/h   # Particle system (init, spawn, update, draw)
│   ├── game/              # Game flow — bridges core logic and render
│   │   ├── ai.c/h         # AI decision logic (pass selection, card play)
│   │   ├── process_input.c/h # Translates mouse/key events into InputCmds
│   │   ├── update.c/h     # Per-frame game state updates
│   │   ├── turn_flow.c/h  # Turn/trick FSM (FlowStep)
│   │   ├── play_phase.c/h # Playing phase rules
│   │   ├── pass_phase.c/h # Passing phase rules
│   │   ├── phase_transitions.c/h # Phase change orchestration
│   │   ├── info_sync.c/h  # Syncs info panel / playability to RenderState
│   │   └── settings_ui.c/h # Settings screen logic
│   ├── audio/
│   │   └── audio.c/h      # Sound effects and music
│   ├── phase2/            # Hollow Hearts modification systems
│   │   ├── contract.h / contract_logic.c/h
│   │   ├── transmutation.h / transmutation_logic.c/h
│   │   ├── vendetta.h / vendetta_logic.c/h
│   │   ├── character.h, effect.h, phase2_state.h
│   │   ├── phase2_defs.c/h # Definition loading
│   │   └── json_parse.c/h  # JSON parser wrapper
│   └── vendor/
│       └── cJSON.c/h      # Third-party JSON library
├── .claude/
│   ├── agents/            # Subagent definitions
│   │   ├── game-developer.md
│   │   ├── architecture-router.md
│   │   ├── dependency-mapper.md
│   │   └── code-auditor.md
│   ├── docs/              # Architecture reference documentation
│   │   ├── 00_INDEX.md
│   │   └── 01-14_*.md
│   └── deps.json          # Dependency map (managed by dependency-mapper)
```

## File Boundaries (strict)

Each file has a single responsibility. Do not leak logic across boundaries.

### `render/` rules

| File | Owns | Does NOT contain |
|------|------|------------------|
| **render.c** | RenderState, sync_hands, render_update/draw, hit-testing, drag state management (start/commit/cancel/snap) | Animation math, easing curves, layout position calculations, card sprite drawing |
| **anim.c** | Animation engine: anim_start, anim_update, anim_setup_toss, speed control, all animation timing constants | Drawing, layout, hit-testing, game state |
| **easing.c** | Pure math: easing curves (ease_apply), lerpf | Anything Raylib, anything stateful |
| **layout.c** | Pure position/rect math: where things go on screen, scaling | Drawing, animation, game state, RenderState |
| **card_render.c** | Drawing a single card (face/back, sprite or procedural) | Layout, animation, game logic |
| **particle.c** | Particle lifecycle (spawn, update, draw) | Cards, layout, game state |

**Key principle:** `render.c` orchestrates but delegates. It calls `layout_*()` for positions, `anim_*()` for animation, `card_render_*()` for drawing. If you need a new position calculation, add it to `layout.c`. If you need a new animation or timing constant, add it to `anim.c/h`. If you need new easing math, add it to `easing.c`.

### `game/` rules

| File | Owns | Does NOT contain |
|------|------|------------------|
| **process_input.c** | Translating raw input into InputCmds and calling render drag API | Animation, layout math, direct CardVisual manipulation |
| **update.c** | Per-frame game state mutation, phase-specific logic dispatch | Rendering, drawing, input polling |
| **turn_flow.c** | Turn/trick state machine (whose turn, AI dispatch, trick resolution) | Rendering, input |
| **info_sync.c** | Copying game state into RenderState display fields (info panel, playability flags) | Game logic decisions, drawing |

**Key principle:** `game/` files read/write GameState and call into `render/` public API. They never manipulate CardVisual fields directly or compute layout positions (except `process_input.c` using `layout_trick_position` for toss classification, which is a geometry query).

### `core/` rules

`core/` is pure game logic. No Raylib includes, no render types, no visual concepts. If a function needs screen positions or card visuals, it does not belong in `core/`.

## Reference Resources

- **Architecture docs** — `.claude/docs/00_INDEX.md` for the full index. Covers subsystem lifecycle, memory management, game loops, events, input — all adapted for C with Raylib.
- **Context7 MCP** — Global MCP server for Raylib API reference. Use to verify function signatures, parameter order, and return types. Available to main agent and all subagents.
- **C Skills** — Three auto-triggered skills (`c-data-structures`, `c-memory-management`, `c-systems-programming`) provide C patterns and best practices. Used by the main agent during implementation, not needed by code-auditor (covered by its prompt).
