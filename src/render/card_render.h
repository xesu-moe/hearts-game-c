#ifndef CARD_RENDER_H
#define CARD_RENDER_H

/* ============================================================
 * @deps-exports: card_render_init(), card_render_shutdown(),
 *                card_render_face(), card_render_back(),
 *                card_render_transmute_init(), card_render_transmute_shutdown(),
 *                card_render_has_transmute_sprite(), card_render_transmute_face(),
 *                card_suit_color(), card_suit_symbol(), card_rank_string()
 * @deps-requires: raylib.h, core/card.h (Card, Suit, Rank)
 * @deps-used-by: render.c, main.c
 * @deps-last-changed: 2026-03-21 — Added transmutation card sprite support
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "core/card.h"

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

/* Set the custom font for procedural card rendering.
 * If not called, falls back to Raylib's default font. */
void card_render_set_font(Font font);

/* Set texture filter for card textures (e.g., TEXTURE_FILTER_BILINEAR for
 * downscaled cards, TEXTURE_FILTER_POINT for full-size crisp rendering). */
void card_render_set_filter(int filter);

/* ---- Transmutation card sprites ---- */

/* Load transmutation card sprites from individual PNGs.
 * Must be called after InitWindow(), typically right after card_render_init().
 * Gracefully handles missing files (falls back to normal card face + ID badge). */
void card_render_transmute_init(void);

/* Unload all transmutation card sprites. Call before card_render_shutdown(). */
void card_render_transmute_shutdown(void);

/* Returns true if the given transmutation ID has a loaded sprite. */
bool card_render_has_transmute_sprite(int transmute_id);

/* Draw a transmuted card face using its dedicated sprite.
 * The sprite replaces the entire card face (no rank/suit text drawn).
 * Caller must check card_render_has_transmute_sprite() first.
 * Parameters match card_render_face() for consistency. */
void card_render_transmute_face(int transmute_id, Vector2 pos, float scale,
                                float opacity, bool hovered, bool selected,
                                float rotation_deg, Vector2 origin);

/* Get the display color for a suit (red for hearts/diamonds, dark gray for others). */
Color card_suit_color(Suit suit);

/* Get ASCII symbol for a suit: "H", "S", "D", "C". */
const char *card_suit_symbol(Suit suit);

/* Get display string for a rank: "2"-"10", "J", "Q", "K", "A". */
const char *card_rank_string(Rank rank);

#endif /* CARD_RENDER_H */
