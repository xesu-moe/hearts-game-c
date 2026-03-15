# Hollow Hearts

A deck-building Hearts modification, built in C with Raylib. Players select designed characters as the figures in their deck, unlocking unique Contracts, Host actions, and Revenges.

## Game Vision & Roadmap

### Phase 1: Vanilla Hearts (current)

Build a fully working standard Hearts game first. All engine systems must be designed with Phase 2 in mind, but no modification mechanics are implemented until vanilla Hearts is complete and stable.

### Phase 2: Hollow Hearts Modifications

Once vanilla Hearts is done, integrate the following systems:

#### Characters (Deck Building)

Players select designed characters to be the figures (Jack, Queen, King) in their deck. Each character has unique art and determines which Contracts, Host actions, and Revenges are available to the player. Character selection happens before the game starts.

#### Contracts (Per-Round Secret Missions)

- Each round, players secretly choose a Contract (e.g., "Don't score any heart", "Obtain 4 club cards")
- Contracts are hidden from other players unless revealed
- Completing a Contract grants a persistent benefit (e.g., "Score -1 heart every round")
- Available Contracts depend on the player's chosen character

#### Host (Losing Player's Round Modifier)

- The player who is losing when a round starts becomes the Host
- The Host chooses a global modifier that affects the entire round for all players
- Available Host actions depend on the Host's chosen character

#### Revenges (Counter to Queen of Spades)

- When a player is hit by the Queen of Spades played by another player, the victim can activate a Revenge against that specific player
- Examples: "Reveal the attacker's Contract", "Lead the next hand"
- Available Revenges depend on the victim's chosen character

### Engine Design Principle

**Build vanilla, architect for mods.** Every system (input, game state, scoring, phases) must have clear extension points for Phase 2 mechanics. Use the Command Pattern for actions, keep game rules data-driven where possible, and ensure the phase FSM can accommodate new phases (character select, contract pick, host action, revenge trigger) without rewriting the core loop.

## Build

```sh
make
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

Every feature or significant change follows a 4-step pipeline using subagents. The main agent orchestrates the workflow and writes all implementation code.

### Step 1: Plan — `@game-developer`

Before implementing any new system, feature, or multi-file change, consult `@game-developer`. This agent:
- Analyzes the request against the current codebase
- Reads architecture docs from `.claude/docs/`
- Produces a structured plan with types, function signatures, integration points, and a checklist

**When to use:** New features, new game systems, complex changes touching 2+ files, architectural decisions.
**When to skip:** Trivial bug fixes, single-line changes, formatting, comments.

### Step 2: Implement — Main Agent

The main agent executes the plan from `@game-developer`:
- Follow the checklist item by item
- Create/modify files as specified in the plan
- Leverage auto-triggered C skills (`c-data-structures`, `c-memory-management`, `c-systems-programming`) for correct patterns — write it right the first time to minimize audit rework
- Use **Context7** MCP server to verify Raylib API signatures before calling them
- Build with `make` to verify compilation

### Step 3: Map Dependencies — `@dependency-mapper`

After implementation, run `@dependency-mapper` to update the dependency graph. This agent:
- Updates `.claude/deps.json` with new/changed dependencies
- Adds/updates in-file dependency headers in `.h` and `.c` files
- Emits an impact alert if the change could break other files

**When to use:** Any change to headers, structs, enums, typedefs, function signatures, or file creation/deletion.
**When to skip:** Implementation-only changes inside function bodies, comments, formatting.

### Step 4: Review — `@code-auditor`

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
@game-developer  →  Structured Plan
    │
    ▼
Main Agent       →  Implementation + `make`
    │
    ▼
@dependency-mapper → deps.json + impact report (if headers/types changed)
    │
    ▼
@code-auditor    →  Audit report → fix issues → done
```

## Project Structure

```
hollow-hearts/
├── CLAUDE.md              # This file
├── Makefile               # Build system
├── src/                   # Source code
├── .claude/
│   ├── agents/            # Subagent definitions
│   │   ├── game-developer.md
│   │   ├── dependency-mapper.md
│   │   └── code-auditor.md
│   ├── docs/              # Architecture reference documentation
│   │   ├── 00_INDEX.md
│   │   └── 01-14_*.md
│   └── deps.json          # Dependency map (managed by dependency-mapper)
```

## Reference Resources

- **Architecture docs** — `.claude/docs/00_INDEX.md` for the full index. Covers subsystem lifecycle, memory management, game loops, events, input — all adapted for C with Raylib.
- **Context7 MCP** — Global MCP server for Raylib API reference. Use to verify function signatures, parameter order, and return types. Available to main agent and all subagents.
- **C Skills** — Three auto-triggered skills (`c-data-structures`, `c-memory-management`, `c-systems-programming`) provide C patterns and best practices. Used by the main agent during implementation, not needed by code-auditor (covered by its prompt).
