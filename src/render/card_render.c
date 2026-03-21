/* ============================================================
 * @deps-implements: card_render.h
 * @deps-requires: card_render.h, card_dimens.h, raylib.h, core/card.h, rlgl.h, string.h
 * @deps-last-changed: 2026-03-21 — Added transmutation sprite loading functions
 * ============================================================ */

#include "card_render.h"
#include "card_dimens.h"

#include <stdio.h>
#include <string.h>

#include "rlgl.h"

#define CARD_CORNER_RADIUS 0.15f

/* ---- Sprite sheet constants ---- */

#define SPRITE_CELL_W 96
#define SPRITE_CELL_H 128

/* ---- Static state ---- */

static Texture2D s_spritesheet;
static Texture2D s_card_back;
static bool      s_textures_loaded = false;

/* ---- Transmutation sprite state ---- */

#define MAX_TRANSMUTE_SPRITE_ID 21

static Texture2D s_transmute_sprites[MAX_TRANSMUTE_SPRITE_ID + 1];
static bool      s_transmute_sprites_loaded = false;

/* Spritesheet rows: Diamonds(0), Spades(1), Hearts(2), Clubs(3)
 * Game enum:        CLUBS(0), DIAMONDS(1), SPADES(2), HEARTS(3) */
static const int SUIT_TO_ROW[SUIT_COUNT] = {
    3, /* SUIT_CLUBS    -> row 3 */
    0, /* SUIT_DIAMONDS -> row 0 */
    1, /* SUIT_SPADES   -> row 1 */
    2, /* SUIT_HEARTS   -> row 2 */
};

/* ---- Helpers ---- */

Color card_suit_color(Suit suit)
{
    switch (suit) {
    case SUIT_HEARTS:
    case SUIT_DIAMONDS:
        return RED;
    default:
        return DARKGRAY;
    }
}

const char *card_suit_symbol(Suit suit)
{
    switch (suit) {
    case SUIT_HEARTS:   return "H";
    case SUIT_SPADES:   return "S";
    case SUIT_DIAMONDS: return "D";
    case SUIT_CLUBS:    return "C";
    default:            return "?";
    }
}

const char *card_rank_string(Rank rank)
{
    static const char *ranks[] = {
        "2", "3", "4", "5", "6", "7", "8", "9", "10",
        "J", "Q", "K", "A"
    };
    int r = (int)rank;
    if (r >= 0 && r < RANK_COUNT) return ranks[r];
    return "?";
}

/* Compute source rectangle within the sprite sheet for a given card.
 * Columns: A(0), 2(1), 3(2), ..., 10(9), J(10), Q(11), K(12) */
static Rectangle card_source_rect(Card card)
{
    if (card.suit < 0 || card.suit >= SUIT_COUNT) return (Rectangle){0};
    int row = SUIT_TO_ROW[card.suit];
    int col = (card.rank == RANK_A) ? 0 : (int)card.rank + 1;
    return (Rectangle){
        .x      = (float)(col * SPRITE_CELL_W),
        .y      = (float)(row * SPRITE_CELL_H),
        .width  = (float)SPRITE_CELL_W,
        .height = (float)SPRITE_CELL_H,
    };
}

/* ---- Init / Shutdown ---- */

bool card_render_init(void)
{
    s_spritesheet = LoadTexture("assets/cards_spritesheet.png");
    s_card_back   = LoadTexture("assets/card_back.png");

    s_textures_loaded = IsTextureValid(s_spritesheet) && IsTextureValid(s_card_back);

    if (!s_textures_loaded) {
        if (IsTextureValid(s_spritesheet)) UnloadTexture(s_spritesheet);
        if (IsTextureValid(s_card_back))   UnloadTexture(s_card_back);
        TraceLog(LOG_WARNING, "CARD_RENDER: Failed to load card textures, "
                              "using procedural fallback");
    }

    return s_textures_loaded;
}

void card_render_shutdown(void)
{
    if (s_textures_loaded) {
        UnloadTexture(s_spritesheet);
        UnloadTexture(s_card_back);
        s_textures_loaded = false;
    }
}

void card_render_set_filter(int filter)
{
    if (!s_textures_loaded) return;
    SetTextureFilter(s_spritesheet, filter);
    SetTextureFilter(s_card_back, filter);

    if (s_transmute_sprites_loaded) {
        for (int i = 0; i <= MAX_TRANSMUTE_SPRITE_ID; i++) {
            if (IsTextureValid(s_transmute_sprites[i]))
                SetTextureFilter(s_transmute_sprites[i], filter);
        }
    }
}

/* ---- Procedural fallbacks ---- */

static void card_render_face_procedural(Card card, Vector2 pos, float scale,
                                        float opacity, bool hovered,
                                        bool selected, float rotation_deg,
                                        Vector2 origin)
{
    float w = CARD_WIDTH_REF * scale;
    float h = CARD_HEIGHT_REF * scale;

    /* Apply rotation transform: translate to pivot, rotate, then offset by origin */
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, 0.0f);
    rlRotatef(rotation_deg, 0.0f, 0.0f, 1.0f);
    rlTranslatef(-origin.x, -origin.y, 0.0f);

    /* All drawing is now relative to (0, 0) = card top-left */
    Rectangle rect = {0, 0, w, h};

    Color bg = WHITE;
    if (selected) {
        bg = (Color){200, 220, 255, (unsigned char)(255 * opacity)};
    } else {
        bg.a = (unsigned char)(255 * opacity);
    }
    DrawRectangleRounded(rect, CARD_CORNER_RADIUS, 4, bg);

    Color border_color;
    if (hovered) {
        border_color = GOLD;
    } else {
        border_color = (Color){100, 100, 100, (unsigned char)(200 * opacity)};
    }
    DrawRectangleRoundedLines(rect, CARD_CORNER_RADIUS, 4, border_color);
    if (hovered) {
        Rectangle inner = {1, 1, w - 2, h - 2};
        DrawRectangleRoundedLines(inner, CARD_CORNER_RADIUS, 4, border_color);
    }

    Color suit_col = card_suit_color(card.suit);
    suit_col.a = (unsigned char)(255 * opacity);

    const char *rank_str = card_rank_string(card.rank);
    const char *suit_str = card_suit_symbol(card.suit);

    int font_size_rank = (int)(20.0f * scale);
    int font_size_suit = (int)(28.0f * scale);

    DrawText(rank_str,
             (int)(6.0f * scale),
             (int)(6.0f * scale),
             font_size_rank, suit_col);

    DrawText(suit_str,
             (int)(6.0f * scale),
             (int)(24.0f * scale),
             font_size_rank, suit_col);

    int suit_w = MeasureText(suit_str, font_size_suit);
    DrawText(suit_str,
             (int)((w - (float)suit_w) * 0.5f),
             (int)((h - (float)font_size_suit) * 0.5f),
             font_size_suit, suit_col);

    int rank_w = MeasureText(rank_str, font_size_rank);
    DrawText(rank_str,
             (int)(w - (float)rank_w - 6.0f * scale),
             (int)(h - 24.0f * scale),
             font_size_rank, suit_col);

    rlPopMatrix();
}

static void card_render_back_procedural(Vector2 pos, float scale, float opacity,
                                        float rotation_deg, Vector2 origin)
{
    float w = CARD_WIDTH_REF * scale;
    float h = CARD_HEIGHT_REF * scale;

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, 0.0f);
    rlRotatef(rotation_deg, 0.0f, 0.0f, 1.0f);
    rlTranslatef(-origin.x, -origin.y, 0.0f);

    Rectangle rect = {0, 0, w, h};

    Color bg = {30, 50, 100, (unsigned char)(255 * opacity)};
    DrawRectangleRounded(rect, CARD_CORNER_RADIUS, 4, bg);

    float margin = 6.0f * scale;
    Rectangle inner = {margin, margin, w - margin * 2.0f, h - margin * 2.0f};
    Color inner_col = {50, 80, 140, (unsigned char)(200 * opacity)};
    DrawRectangleRounded(inner, CARD_CORNER_RADIUS, 4, inner_col);

    Color border = {20, 40, 80, (unsigned char)(200 * opacity)};
    DrawRectangleRoundedLines(rect, CARD_CORNER_RADIUS, 4, border);

    rlPopMatrix();
}

/* ---- Public rendering ---- */

void card_render_face(Card card, Vector2 pos, float scale,
                      float opacity, bool hovered, bool selected,
                      float rotation_deg, Vector2 origin)
{
    if (!s_textures_loaded) {
        card_render_face_procedural(card, pos, scale, opacity, hovered,
                                    selected, rotation_deg, origin);
        return;
    }

    float w = CARD_WIDTH_REF * scale;
    float h = CARD_HEIGHT_REF * scale;

    Rectangle src = card_source_rect(card);
    Rectangle dst = {pos.x, pos.y, w, h};

    Color tint;
    if (selected) {
        tint = (Color){200, 220, 255, (unsigned char)(255 * opacity)};
    } else {
        tint = (Color){255, 255, 255, (unsigned char)(255 * opacity)};
    }

    DrawTexturePro(s_spritesheet, src, dst, origin, rotation_deg, tint);

    if (hovered) {
        /* Draw hover border using rlgl transform to match card rotation */
        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, 0.0f);
        rlRotatef(rotation_deg, 0.0f, 0.0f, 1.0f);
        rlTranslatef(-origin.x, -origin.y, 0.0f);

        Rectangle rect = {0, 0, w, h};
        DrawRectangleRoundedLines(rect, CARD_CORNER_RADIUS, 4, GOLD);
        Rectangle inner = {1, 1, w - 2, h - 2};
        DrawRectangleRoundedLines(inner, CARD_CORNER_RADIUS, 4, GOLD);

        rlPopMatrix();
    }
}

void card_render_back(Vector2 pos, float scale, float opacity,
                      float rotation_deg, Vector2 origin)
{
    if (!s_textures_loaded) {
        card_render_back_procedural(pos, scale, opacity, rotation_deg, origin);
        return;
    }

    float w = CARD_WIDTH_REF * scale;
    float h = CARD_HEIGHT_REF * scale;

    Rectangle src = {0, 0, (float)s_card_back.width, (float)s_card_back.height};
    Rectangle dst = {pos.x, pos.y, w, h};

    Color tint = {255, 255, 255, (unsigned char)(255 * opacity)};

    DrawTexturePro(s_card_back, src, dst, origin, rotation_deg, tint);
}

/* ---- Transmutation card sprites ---- */

void card_render_transmute_init(void)
{
    memset(s_transmute_sprites, 0, sizeof(s_transmute_sprites));

    /* Keep in sync with assets/defs/transmutations.json IDs */
    static const struct { int id; const char *path; } SPRITE_MAP[] = {
        {  4, "assets/sprites-temp/gatherer-final.png"   },
        {  6, "assets/sprites-temp/pendulum-final.png"   },
        {  7, "assets/sprites-temp/duel-final.png"       },
        {  9, "assets/sprites-temp/mirror-final.png"     },
        { 12, "assets/sprites-temp/trap-final.png"       },
        { 13, "assets/sprites-temp/shield-final.png"     },
        { 14, "assets/sprites-temp/curse-final.png"      },
        { 15, "assets/sprites-temp/anchor-final.png"     },
        { 16, "assets/sprites-temp/crown-final.png"      },
        { 17, "assets/sprites-temp/parasite-final.png"   },
        { 18, "assets/sprites-temp/bounty-final.png"     },
        { 19, "assets/sprites-temp/inversion-final.png"  },
        { 20, "assets/sprites-temp/binding-final.png"    },
    };
    int count = (int)(sizeof(SPRITE_MAP) / sizeof(SPRITE_MAP[0]));

    int loaded = 0;
    for (int i = 0; i < count; i++) {
        Texture2D tex = LoadTexture(SPRITE_MAP[i].path);
        if (IsTextureValid(tex)) {
            SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
            s_transmute_sprites[SPRITE_MAP[i].id] = tex;
            loaded++;
        } else {
            TraceLog(LOG_WARNING, "CARD_RENDER: Failed to load transmute sprite: %s",
                     SPRITE_MAP[i].path);
        }
    }

    s_transmute_sprites_loaded = (loaded > 0);
    TraceLog(LOG_INFO, "CARD_RENDER: Loaded %d/%d transmutation sprites", loaded, count);
}

void card_render_transmute_shutdown(void)
{
    for (int i = 0; i <= MAX_TRANSMUTE_SPRITE_ID; i++) {
        if (IsTextureValid(s_transmute_sprites[i]))
            UnloadTexture(s_transmute_sprites[i]);
    }
    memset(s_transmute_sprites, 0, sizeof(s_transmute_sprites));
    s_transmute_sprites_loaded = false;
}

bool card_render_has_transmute_sprite(int transmute_id)
{
    if (transmute_id < 0 || transmute_id > MAX_TRANSMUTE_SPRITE_ID)
        return false;
    return IsTextureValid(s_transmute_sprites[transmute_id]);
}

void card_render_transmute_face(int transmute_id, Vector2 pos, float scale,
                                float opacity, bool hovered, bool selected,
                                float rotation_deg, Vector2 origin)
{
    if (transmute_id < 0 || transmute_id > MAX_TRANSMUTE_SPRITE_ID) return;
    Texture2D tex = s_transmute_sprites[transmute_id];

    float w = CARD_WIDTH_REF * scale;
    float h = CARD_HEIGHT_REF * scale;

    Rectangle src = {0, 0, (float)tex.width, (float)tex.height};
    Rectangle dst = {pos.x, pos.y, w, h};

    Color tint;
    if (selected) {
        tint = (Color){200, 220, 255, (unsigned char)(255 * opacity)};
    } else {
        tint = (Color){255, 255, 255, (unsigned char)(255 * opacity)};
    }

    DrawTexturePro(tex, src, dst, origin, rotation_deg, tint);

    if (hovered) {
        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, 0.0f);
        rlRotatef(rotation_deg, 0.0f, 0.0f, 1.0f);
        rlTranslatef(-origin.x, -origin.y, 0.0f);

        Rectangle rect = {0, 0, w, h};
        DrawRectangleRoundedLines(rect, CARD_CORNER_RADIUS, 4, GOLD);
        Rectangle inner = {1, 1, w - 2, h - 2};
        DrawRectangleRoundedLines(inner, CARD_CORNER_RADIUS, 4, GOLD);

        rlPopMatrix();
    }
}
