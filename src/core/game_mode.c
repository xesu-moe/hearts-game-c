/* ============================================================
 * @deps-implements: core/game_mode.h
 * @deps-requires: core/game_mode.h
 * @deps-last-changed: 2026-04-15 — vanilla_plan.md Step 1: introduce GameMode
 * ============================================================ */

#include "game_mode.h"

/* Sized on GAMEMODE_COUNT so adding a new GameMode value without extending
 * this array is a compile-time error. */
const char *GAMEMODE_LABELS[GAMEMODE_COUNT] = {
    "Transmutations",
    "Vanilla",
    "Dragon Hearts"
};
