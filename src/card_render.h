#ifndef CARD_RENDER_H
#define CARD_RENDER_H

/* ============================================================
 * @deps-exports: card_render_init(), card_render_shutdown(),
 *                card_render_face(), card_render_back(),
 *                card_suit_color(), card_suit_symbol(), card_rank_string()
 * @deps-requires: raylib.h, card.h (Card, Suit, Rank)
 * @deps-used-by: card_render.c, render.c, main.c
 * @deps-last-changed: 2026-03-14 — Added rotation_deg and origin params to face/back
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "card.h"

/* Load card sprite sheet and back texture. Must be called after InitWindow().
 * Returns true on success, false if textures failed (falls back to procedural). */
bool card_render_init(void);

/* Unload card textures. Call before CloseWindow(). */
void card_render_shutdown(void);

/* Draw a face-up card at the given position with scale, opacity, and rotation.
 * pos: the pivot point in screen space (where origin maps to).
 * origin: rotation pivot relative to the card (e.g., bottom-center for fan).
 * rotation_deg: rotation angle in degrees around the origin.
 * hovered: draw yellow border. selected: draw blue tint + lift handled by caller. */
void card_render_face(Card card, Vector2 pos, float scale,
                      float opacity, bool hovered, bool selected,
                      float rotation_deg, Vector2 origin);

/* Draw a face-down card (back side) at the given position with rotation. */
void card_render_back(Vector2 pos, float scale, float opacity,
                      float rotation_deg, Vector2 origin);

/* Get the display color for a suit (red for hearts/diamonds, dark gray for others). */
Color card_suit_color(Suit suit);

/* Get ASCII symbol for a suit: "H", "S", "D", "C". */
const char *card_suit_symbol(Suit suit);

/* Get display string for a rank: "2"-"10", "J", "Q", "K", "A". */
const char *card_rank_string(Rank rank);

#endif /* CARD_RENDER_H */
