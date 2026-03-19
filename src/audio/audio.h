#ifndef AUDIO_H
#define AUDIO_H

/* ============================================================
 * @deps-exports: MusicContext (MUSIC_NONE, MUSIC_BACKGROUND, MUSIC_COUNT),
 *                SfxId (SFX_CARD_PLAY, SFX_CARD_DEAL, SFX_HEARTS_BROKEN,
 *                SFX_TRANSMUTE, SFX_SCORE_TICK, SFX_COUNT),
 *                AudioState, audio_init(), audio_shutdown(), audio_update(),
 *                audio_set_music(), audio_play_sfx(), audio_apply_settings(),
 *                audio_start_stagger()
 * @deps-requires: raylib.h (Music, Sound), core/settings.h (GameSettings)
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-20 — audio_update() takes anim_speed param instead of depending on render/anim.h
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
    SFX_SCORE_TICK,
    SFX_COUNT
} SfxId;

#define MAX_SFX_STAGGERS 2

typedef struct SfxStagger {
    bool    active;
    SfxId   sfx;
    int     remaining;       /* sounds left to fire */
    float   interval;        /* base interval between sounds (seconds) */
    float   timer;           /* countdown to next sound */
    bool    scale_by_anim;   /* multiply interval by anim_speed param */
} SfxStagger;

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
    SfxStagger   staggers[MAX_SFX_STAGGERS];
} AudioState;

void audio_init(AudioState *a, const GameSettings *s);
void audio_shutdown(AudioState *a);
void audio_update(AudioState *a, float dt, float anim_speed);
void audio_set_music(AudioState *a, MusicContext ctx);
void audio_play_sfx(AudioState *a, SfxId id);
void audio_apply_settings(AudioState *a, const GameSettings *s);

/* Start a stagger sequence: play sfx `count` times at `base_interval` apart.
 * If scale_anim is true, interval is multiplied by the anim_speed param each tick.
 * First sound fires immediately. Reuses first inactive slot, or overwrites slot 0. */
void audio_start_stagger(AudioState *a, SfxId sfx, int count,
                         float base_interval, bool scale_anim);

#endif /* AUDIO_H */
