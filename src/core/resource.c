/* ============================================================
 * resource.c — Asset loading abstraction.
 *
 * Without HH_EMBEDDED: thin passthrough to Raylib / stdio.
 * With HH_EMBEDDED: looks up compiled-in byte arrays.
 * ============================================================ */

#include "core/resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Embedded mode — assets compiled into the binary
 * ================================================================ */
#ifdef HH_EMBEDDED

#include "embedded_assets.h"

typedef struct {
    const char          *path;
    const unsigned char *data;
    unsigned int         size;
} EmbeddedAsset;

static const EmbeddedAsset s_assets[] = {
    {"assets/cards_spritesheet.png",                assets_cards_spritesheet_png,                assets_cards_spritesheet_png_SIZE},
    {"assets/card_back.png",                        assets_card_back_png,                        assets_card_back_png_SIZE},
    {"assets/sprites-temp/redjoker-final.png",      assets_sprites_temp_redjoker_final_png,      assets_sprites_temp_redjoker_final_png_SIZE},
    {"assets/sprites-temp/blackjoker-final.png",    assets_sprites_temp_blackjoker_final_png,    assets_sprites_temp_blackjoker_final_png_SIZE},
    {"assets/sprites-temp/shadowqueen-final.png",   assets_sprites_temp_shadowqueen_final_png,   assets_sprites_temp_shadowqueen_final_png_SIZE},
    {"assets/sprites-temp/martyr-final.png",        assets_sprites_temp_martyr_final_png,        assets_sprites_temp_martyr_final_png_SIZE},
    {"assets/sprites-temp/gatherer-final.png",      assets_sprites_temp_gatherer_final_png,      assets_sprites_temp_gatherer_final_png_SIZE},
    {"assets/sprites-temp/rogue-final.png",         assets_sprites_temp_rogue_final_png,         assets_sprites_temp_rogue_final_png_SIZE},
    {"assets/sprites-temp/pendulum-final.png",      assets_sprites_temp_pendulum_final_png,      assets_sprites_temp_pendulum_final_png_SIZE},
    {"assets/sprites-temp/duel-final.png",          assets_sprites_temp_duel_final_png,          assets_sprites_temp_duel_final_png_SIZE},
    {"assets/sprites-temp/fog-final.png",           assets_sprites_temp_fog_final_png,           assets_sprites_temp_fog_final_png_SIZE},
    {"assets/sprites-temp/mirror-final.png",        assets_sprites_temp_mirror_final_png,        assets_sprites_temp_mirror_final_png_SIZE},
    {"assets/sprites-temp/roulette-final.png",      assets_sprites_temp_roulette_final_png,      assets_sprites_temp_roulette_final_png_SIZE},
    {"assets/sprites-temp/trap-final.png",          assets_sprites_temp_trap_final_png,          assets_sprites_temp_trap_final_png_SIZE},
    {"assets/sprites-temp/shield-final.png",        assets_sprites_temp_shield_final_png,        assets_sprites_temp_shield_final_png_SIZE},
    {"assets/sprites-temp/curse-final.png",         assets_sprites_temp_curse_final_png,         assets_sprites_temp_curse_final_png_SIZE},
    {"assets/sprites-temp/anchor-final.png",        assets_sprites_temp_anchor_final_png,        assets_sprites_temp_anchor_final_png_SIZE},
    {"assets/sprites-temp/crown-final.png",         assets_sprites_temp_crown_final_png,         assets_sprites_temp_crown_final_png_SIZE},
    {"assets/sprites-temp/parasite-final.png",      assets_sprites_temp_parasite_final_png,      assets_sprites_temp_parasite_final_png_SIZE},
    {"assets/sprites-temp/bounty-final.png",        assets_sprites_temp_bounty_final_png,        assets_sprites_temp_bounty_final_png_SIZE},
    {"assets/sprites-temp/inversion-final.png",     assets_sprites_temp_inversion_final_png,     assets_sprites_temp_inversion_final_png_SIZE},
    {"assets/sprites-temp/binding-final.png",       assets_sprites_temp_binding_final_png,       assets_sprites_temp_binding_final_png_SIZE},
    {"assets/sprites-temp/joker-final.png",         assets_sprites_temp_joker_final_png,         assets_sprites_temp_joker_final_png_SIZE},
    {"assets/fonts/Lora-VariableFont_wght.ttf",     assets_fonts_Lora_VariableFont_wght_ttf,    assets_fonts_Lora_VariableFont_wght_ttf_SIZE},
    {"assets/music/background.ogg",                 assets_music_background_ogg,                 assets_music_background_ogg_SIZE},
    {"assets/sounds/card-play.ogg",                 assets_sounds_card_play_ogg,                 assets_sounds_card_play_ogg_SIZE},
    {"assets/sounds/card-deal.ogg",                 assets_sounds_card_deal_ogg,                 assets_sounds_card_deal_ogg_SIZE},
    {"assets/shaders/fog.fs",                       assets_shaders_fog_fs,                       assets_shaders_fog_fs_SIZE},
    {"assets/defs/contracts.json",                  assets_defs_contracts_json,                  assets_defs_contracts_json_SIZE},
    {"assets/defs/transmutations.json",             assets_defs_transmutations_json,             assets_defs_transmutations_json_SIZE},
    {NULL, NULL, 0}
};

static const EmbeddedAsset *find_asset(const char *path)
{
    for (int i = 0; s_assets[i].path; i++) {
        if (strcmp(s_assets[i].path, path) == 0)
            return &s_assets[i];
    }
    return NULL;
}

Texture2D res_load_texture(const char *path)
{
    const EmbeddedAsset *a = find_asset(path);
    if (!a) {
        TraceLog(LOG_WARNING, "RESOURCE: Embedded asset not found: %s", path);
        return (Texture2D){0};
    }
    Image img = LoadImageFromMemory(".png", a->data, (int)a->size);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

Font res_load_font(const char *path, int font_size)
{
    const EmbeddedAsset *a = find_asset(path);
    if (!a) {
        TraceLog(LOG_WARNING, "RESOURCE: Embedded asset not found: %s", path);
        return GetFontDefault();
    }
    return LoadFontFromMemory(".ttf", a->data, (int)a->size, font_size, NULL, 0);
}

Music res_load_music(const char *path)
{
    const EmbeddedAsset *a = find_asset(path);
    if (!a) {
        TraceLog(LOG_WARNING, "RESOURCE: Embedded asset not found: %s", path);
        return (Music){0};
    }
    /* Data must remain valid for the lifetime of the Music object.
     * Embedded arrays are static globals, so this is safe. */
    return LoadMusicStreamFromMemory(".ogg", a->data, (int)a->size);
}

Sound res_load_sound(const char *path)
{
    const EmbeddedAsset *a = find_asset(path);
    if (!a) {
        TraceLog(LOG_WARNING, "RESOURCE: Embedded asset not found: %s", path);
        return (Sound){0};
    }
    Wave wave = LoadWaveFromMemory(".ogg", a->data, (int)a->size);
    Sound snd = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return snd;
}

Shader res_load_shader(const char *vs_path, const char *fs_path)
{
    const char *vs_code = NULL;
    const char *fs_code = NULL;

    if (vs_path) {
        const EmbeddedAsset *a = find_asset(vs_path);
        if (a) vs_code = (const char *)a->data; /* null-terminated by embed script */
    }
    if (fs_path) {
        const EmbeddedAsset *a = find_asset(fs_path);
        if (a) fs_code = (const char *)a->data; /* null-terminated by embed script */
    }

    return LoadShaderFromMemory(vs_code, fs_code);
}

char *res_read_file_text(const char *path)
{
    const EmbeddedAsset *a = find_asset(path);
    if (a) {
        /* Return a malloc'd copy — caller expects to free() it.
         * The data is already null-terminated by the embed script. */
        char *buf = malloc(a->size);
        if (!buf) return NULL;
        memcpy(buf, a->data, a->size);
        return buf;
    }

    /* Fall through to file I/O for non-embedded paths (e.g. settings.json) */
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[read] = '\0';
    return buf;
}

/* ================================================================
 * Dev mode — passthrough to Raylib / stdio
 * ================================================================ */
#else /* !HH_EMBEDDED */

Texture2D res_load_texture(const char *path)
{
    return LoadTexture(path);
}

Font res_load_font(const char *path, int font_size)
{
    return LoadFontEx(path, font_size, NULL, 0);
}

Music res_load_music(const char *path)
{
    return LoadMusicStream(path);
}

Sound res_load_sound(const char *path)
{
    return LoadSound(path);
}

Shader res_load_shader(const char *vs_path, const char *fs_path)
{
    return LoadShader(vs_path, fs_path);
}

char *res_read_file_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[read] = '\0';
    return buf;
}

#endif /* HH_EMBEDDED */
