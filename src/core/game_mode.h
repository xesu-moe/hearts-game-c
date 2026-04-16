#ifndef GAME_MODE_H
#define GAME_MODE_H

/* ============================================================
 * @deps-exports: enum GameMode (GAMEMODE_TRANSMUTATIONS, GAMEMODE_VANILLA,
 *                GAMEMODE_DRAGON_HEARTS, GAMEMODE_COUNT),
 *                extern GAMEMODE_LABELS[],
 *                gamemode_uses_phase2()
 * @deps-requires: (none — leaf)
 * @deps-used-by: net/protocol.h (propagates transitively to server_game.c,
 *                server_net.c, room.c, render.c, process_input.c, online_ui.c)
 * @deps-last-changed: 2026-04-15 — vanilla_plan.md Step 1: introduce GameMode
 * ============================================================ */

#include <stdbool.h>
#include <stdint.h>

/* Game mode selected at room creation. Values are stable — they travel on
 * the wire in NetMsgStartGame.gamemode and are stored as uint8_t in
 * Room.gamemode / ServerGame.gamemode / OnlineUIState.gamemode. Do NOT
 * renumber. */
typedef enum GameMode {
    GAMEMODE_TRANSMUTATIONS = 0, /* Hollow Hearts: contracts + transmutations (default) */
    GAMEMODE_VANILLA        = 1, /* Classic Hearts: no Phase 2, fixed pass rotation */
    GAMEMODE_DRAGON_HEARTS  = 2, /* Placeholder — not yet implemented */
    GAMEMODE_COUNT
} GameMode;

/* Short display labels, index-aligned with the enum values. Defined in
 * src/core/game_mode.c. Size = GAMEMODE_COUNT. */
extern const char *GAMEMODE_LABELS[];

/* True if the mode uses Phase 2 mechanics (contracts, transmutations, contract
 * draft, dealer phase, rogue/duel effects). Single decision point — every
 * guard should go through this helper so adding a new mode is cheap. */
static inline bool gamemode_uses_phase2(GameMode mode) {
    return mode != GAMEMODE_VANILLA;
}

#endif /* GAME_MODE_H */
