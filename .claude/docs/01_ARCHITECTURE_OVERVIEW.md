# 01 — Runtime Engine Architecture Overview

## The Layer Cake

A game engine is a stack of interdependent subsystems. Lower layers provide services to upper layers. For our top-down RPG in C/Raylib, the relevant layers (bottom to top) are:

```
┌─────────────────────────────────────────────────────┐
│              GAME-SPECIFIC SYSTEMS                   │
│  Combat, Inventory, Dialogue, Quests, AI, Enemies   │
├─────────────────────────────────────────────────────┤
│            GAMEPLAY FOUNDATION                       │
│  Game Object Model, Events, State Machines,          │
│  World Loading, High-Level Game Flow (FSM)           │
├─────────────────────────────────────────────────────┤
│              FRONT END / HUD                         │
│  In-game menus, HUD, health bars, minimaps          │
├─────────────────────────────────────────────────────┤
│           DEBUG & PROFILING TOOLS                    │
│  Logging, debug drawing, in-game console, profiling │
├─────────────────────────────────────────────────────┤
│           LOW-LEVEL ENGINE SYSTEMS                   │
│  Game Loop, Input Abstraction, Resource Manager,     │
│  Memory Allocators, Config/Settings                  │
├─────────────────────────────────────────────────────┤
│               CORE SYSTEMS                           │
│  Math library, String hashing, RNG, Assertions,      │
│  Containers (dynamic arrays, hash maps)              │
├─────────────────────────────────────────────────────┤
│          PLATFORM / THIRD-PARTY LAYER                │
│  Raylib (rendering, audio, input, windowing),        │
│  C standard library, OS file system                  │
├─────────────────────────────────────────────────────┤
│              HARDWARE / OS                           │
│  CPU, GPU, RAM, Storage, OS                          │
└─────────────────────────────────────────────────────┘
```

## Subsystem Responsibilities

### Platform Layer (Raylib)
Raylib handles: window creation, 2D rendering, texture loading, audio playback, keyboard/gamepad input, basic collision shapes, frame timing. We build on top of these, not around them.

### Core Systems
Things every other system depends on:
- **Assertions**: Runtime checks stripped from release builds
- **Memory allocators**: Custom allocators for performance-critical paths
- **Math library**: 2D vector ops, AABB, rectangle math (Raylib provides basics; extend as needed)
- **String hashing**: Convert strings to integer IDs for fast comparison
- **Random number generator**: Game-quality RNG with seedable state
- **Containers**: Dynamic arrays, hash maps (C has no built-in containers)
- **Logging**: Categorized debug output with verbosity levels

### Low-Level Engine Systems
- **Game loop**: The master loop driving all subsystem updates
- **Input abstraction**: Map raw keys/buttons to game actions
- **Resource manager**: Load, cache, and manage game assets (textures, sounds, maps, data files)
- **Engine configuration**: Read settings from files (window size, volume, keybinds)
- **Subsystem start-up/shut-down**: Ordered initialization and cleanup

### Debug & Profiling
- **Debug drawing**: Overlay collision shapes, paths, regions on the game world
- **In-game console**: Execute commands at runtime for testing
- **Profiling**: Measure per-frame time spent in each subsystem
- **Logging**: Timestamped, categorized output to console and file

### Gameplay Foundation
- **Game object model**: How entities (player, enemies, items, projectiles) are represented
- **Component system**: Modular behaviors attached to game objects
- **Event system**: Decoupled inter-object communication
- **World management**: Loading/unloading map chunks, managing static and dynamic elements
- **State machines**: High-level game flow (menus, gameplay, pause, cutscenes) and per-object behavior
- **Update scheduling**: Batched, phased updates respecting inter-object dependencies

### Game-Specific Systems
Built on the gameplay foundation. Specific to your RPG:
- Combat (damage, hit detection, cooldowns)
- Inventory and equipment
- Dialogue and quest tracking
- Enemy AI (patrol, chase, attack patterns)
- Loot and item drops
- Experience and leveling

## Dependency Order

Subsystems must start in dependency order and shut down in reverse. The typical order for our engine:

```
START-UP ORDER (first to last):
1. Logging          (no dependencies)
2. Memory system    (no dependencies)  
3. Config system    (needs: file I/O)
4. Raylib init      (InitWindow, InitAudioDevice)
5. Resource manager (needs: Raylib, file I/O, memory)
6. Input system     (needs: Raylib)
7. Audio manager    (needs: Raylib, resource manager)
8. Debug tools      (needs: Raylib, logging)
9. Game world       (needs: resource manager)
10. Game object mgr (needs: game world, events)
11. Event system    (needs: game object mgr)
12. Game state FSM  (needs: everything above)

SHUT-DOWN ORDER: reverse of the above (12 → 1)
```

## Practical Advice

- **Don't over-architect early**: Start with the simplest possible version of each system. Refactor when you hit real limitations, not imagined ones.
- **Raylib is your friend**: It already handles the hardest low-level problems (GPU rendering, audio mixing, cross-platform input). Leverage it fully.
- **Flat is better than nested**: In C, favor flat data structures and explicit function calls over deep abstraction hierarchies. A switch statement is often clearer than a vtable.
- **Data locality matters**: Keep arrays of similar things together in memory. An array of 100 enemy positions is faster to iterate than 100 enemy structs each containing a position buried among other fields.
