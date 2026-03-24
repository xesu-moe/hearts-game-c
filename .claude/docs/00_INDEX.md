# Game Engine Architecture Reference — C / Raylib Edition

> Curated documentation for a **top-down 2D action RPG** built with **C and Raylib**.
> Derived from *Game Engine Architecture* by Jason Gregory (A K Peters, 2009).
> All patterns have been translated from C++ to idiomatic C with Raylib-specific examples where applicable.

## How to Use This Documentation

You are a **game-developer subagent** specializing in engine architecture. Load the file most relevant to your current task. Each file is self-contained with:
- **Architectural concepts** (the "why")
- **C implementation patterns** (the "how")  
- **Raylib integration notes** (the "with what")
- **Pitfalls & best practices** (the "watch out")

## File Index

| File | Topic | When to Read |
|------|-------|-------------|
| `01_ARCHITECTURE_OVERVIEW.md` | Runtime engine layer diagram, subsystem responsibilities, dependency order | Starting a new system, understanding where it fits |
| `02_SUBSYSTEM_LIFECYCLE.md` | Start-up/shut-down ordering, singleton pattern in C, initialization best practices | Setting up or tearing down engine subsystems |
| `03_MEMORY_MANAGEMENT.md` | Stack allocators, pool allocators, aligned allocation, frame allocators, fragmentation avoidance | Allocating memory, optimizing cache performance |
| `04_GAME_LOOP.md` | Game loop styles, fixed vs variable timestep, delta time, frame governing, Clock implementation | Implementing or modifying the main loop |
| `05_TIME_AND_CLOCKS.md` | Abstract timelines, real time vs game time, time scaling, pause/single-step, timer precision | Time management, pause systems, slow-motion |
| `06_GAME_OBJECTS.md` | Object models (monolithic, component, property-centric), ECS patterns, Hydro Thunder C example | Designing entities, components, world structure |
| `07_GAME_WORLD.md` | Static vs dynamic elements, world chunks, level loading/streaming, spatial queries | World structure, level management, loading |
| `08_UPDATE_LOOP.md` | Batched updates, phased updates, bucketed updates, inter-object dependencies, one-frame-off bugs | Updating game objects each frame, ordering issues |
| `09_EVENTS.md` | Event system design, event objects, handlers, queuing, chains of responsibility, registration | Implementing inter-object communication |
| `10_INPUT_HID.md` | Input device abstraction, button states, chords, action mapping, dead zones | Processing player input with Raylib |
| `11_DEBUG_TOOLS.md` | Logging, debug drawing, in-game console, profiling, screen capture, cheats | Building development/debug infrastructure |
| `12_MATH_ESSENTIALS.md` | 2D vectors, matrices, random numbers, fixed-point time, AABB, collision primitives | Math utilities for a 2D top-down game |
| `13_RESOURCE_MANAGEMENT.md` | File system abstraction, resource manager, asset pipeline, offline processing | Loading and managing game assets |
| `14_DATA_DRIVEN_DESIGN.md` | Data-driven engines, configuration, scripting integration, world editor concepts | Making the engine configurable without recompilation |

## Key Principles for This Project

1. **C, not C++**: No classes, no templates, no exceptions. Use structs, function pointers, and explicit memory management.
2. **Raylib handles rendering and input**: Don't reimplement what Raylib provides. Focus engine architecture on gameplay systems, game objects, events, and world management.
3. **2D top-down**: Skip 3D math (quaternions, 3D matrices, skeletal animation). Focus on 2D vectors, rotation angles, sprite animation, and tile-based or chunk-based worlds.
4. **Simplicity over generality**: This is a single game, not a reusable engine. Favor simple, direct solutions over over-engineered abstractions.
5. **Cache-friendly data layout**: Prefer arrays of structs (or structs of arrays) over pointer-heavy linked structures. Keep hot data contiguous.

## Raylib Quick Reference

Raylib already provides these — do NOT reimplement:
- **Window/rendering**: `InitWindow`, `BeginDrawing`, `EndDrawing`, `DrawTexture*`
- **Input**: `IsKeyDown`, `IsKeyPressed`, `GetGamepadAxis*`, `IsGamepadButtonPressed`
- **Audio**: `InitAudioDevice`, `LoadSound`, `PlaySound`, `LoadMusicStream`
- **Textures/Sprites**: `LoadTexture`, `DrawTextureRec`, `DrawTexturePro`
- **Basic math**: `Vector2Add`, `Vector2Scale`, `CheckCollisionRecs`, `CheckCollisionCircles`
- **File I/O**: `LoadFileData`, `SaveFileData`, `FileExists`
- **Timing**: `GetFrameTime`, `GetTime`, `SetTargetFPS`

Focus your architecture on: **game objects, components, events, world management, state machines, resource pipelines, and gameplay logic**.
