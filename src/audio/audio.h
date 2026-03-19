#ifndef AUDIO_H
#define AUDIO_H

/* ============================================================
 * @deps-exports: MusicContext, SfxId, AudioState,
 *                audio_init(), audio_shutdown(), audio_update(),
 *                audio_set_music(), audio_play_sfx(), audio_apply_settings()
 * @deps-requires: raylib.h (Music, Sound), core/settings.h (GameSettings)
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-19 — Created
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

/* Forward declaration */
typedef struct GameSettings GameSettings;

typedef enum MusicContext {
    MUSIC_NONE = -1,
    MUSIC_BACKGROUND,
    MUSIC_COUNT
} MusicContext;

typedef enum SfxId {
    SFX_CARD_PLAY,
    SFX_CARD_DEAL,
    SFX_HEARTS_BROKEN,
    SFX_TRANSMUTE,
    SFX_COUNT
} SfxId;

typedef struct AudioState {
    Music        tracks[MUSIC_COUNT];
    bool         track_loaded[MUSIC_COUNT];
    Sound        sfx[SFX_COUNT];
    bool         sfx_loaded[SFX_COUNT];
    MusicContext current;      /* playing / fading in */
    MusicContext previous;     /* fading out, or MUSIC_NONE */
    float        fade_timer;
    float        master_vol;
    float        music_vol;
    float        sfx_vol;
} AudioState;

void audio_init(AudioState *a, const GameSettings *s);
void audio_shutdown(AudioState *a);
void audio_update(AudioState *a, float dt);
void audio_set_music(AudioState *a, MusicContext ctx);
void audio_play_sfx(AudioState *a, SfxId id);
void audio_apply_settings(AudioState *a, const GameSettings *s);

#endif /* AUDIO_H */
