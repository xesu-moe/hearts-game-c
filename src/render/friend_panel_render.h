#ifndef FRIEND_PANEL_RENDER_H
#define FRIEND_PANEL_RENDER_H

/* ============================================================
 * @deps-exports: friend_panel_render_input, friend_panel_render_draw,
 *                FRIEND_PANEL_WIDTH, FRIEND_ENTRY_HEIGHT, FRIEND_SEARCH_HEIGHT
 * @deps-requires: game/friend_panel.h, raylib.h
 * @deps-used-by: game/online_ui.c
 * @deps-last-changed: 2026-04-06 — Created
 * ============================================================ */

#include "../game/friend_panel.h"
#include <raylib.h>

#define FRIEND_PANEL_WIDTH   220
#define FRIEND_ENTRY_HEIGHT   36
#define FRIEND_SEARCH_HEIGHT  32

/* Process input for the friend panel (mouse, keyboard, scroll).
   Call before draw each frame. */
void friend_panel_render_input(FriendPanelState *state, Rectangle panel_rect);

/* Draw the friend panel. */
void friend_panel_render_draw(FriendPanelState *state, Rectangle panel_rect, Font font);

#endif /* FRIEND_PANEL_RENDER_H */
