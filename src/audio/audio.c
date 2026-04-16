/* ============================================================
 * @deps-implements: audio/audio.h
 * @deps-requires: audio/audio.h, core/settings.h, raylib.h
 * @deps-last-changed: 2026-03-20 — Removed render/anim.h dep; anim_speed passed as parameter
 * ============================================================ */

#include "audio/audio.h"

#include <stddef.h>

#include "core/resource.h"
#include "core/settings.h"

#define FADE_DURATION 1.0f

static const char *music_paths[MUSIC_COUNT] = {
    "assets/music/background.ogg",
};

static const char *sfx_paths[SFX_COUNT] = {
    "assets/sounds/card-play.ogg",
    "assets/sounds/card-deal.ogg",
    NULL,  /* SFX_HEARTS_BROKEN — no asset yet */
    NULL,  /* SFX_TRANSMUTE — no asset yet */
    "assets/sounds/score-tick.ogg",
};

void audio_init(AudioState *a, const GameSettings *s)
{
    SetAudioStreamBufferSizeDefault(8192);
    InitAudioDevice();

    for (int i = 0; i < MUSIC_COUNT; i++) {
        a->tracks[i] = res_load_music(music_paths[i]);
        a->tracks[i].looping = true;
        a->track_loaded[i] = (a->tracks[i].stream.sampleRate > 0);
    }

    for (int i = 0; i < SFX_COUNT; i++) {
        if (sfx_paths[i]) {
            a->sfx[i] = res_load_sound(sfx_paths[i]);
            a->sfx_loaded[i] = (a->sfx[i].frameCount > 0);
        } else {
            a->sfx_loaded[i] = false;
        }
    }

    a->current = MUSIC_NONE;
    a->previous = MUSIC_NONE;
    a->fade_timer = 0.0f;

    for (int i = 0; i < MAX_SFX_STAGGERS; i++)
        a->staggers[i].active = false;

    audio_apply_settings(a, s);
}

void audio_shutdown(AudioState *a)
{
    for (int i = 0; i < MUSIC_COUNT; i++) {
        if (a->track_loaded[i]) UnloadMusicStream(a->tracks[i]);
    }
    for (int i = 0; i < SFX_COUNT; i++) {
        if (a->sfx_loaded[i]) UnloadSound(a->sfx[i]);
    }
    CloseAudioDevice();
}

static void update_staggers(AudioState *a, float dt, float anim_speed)
{
    for (int i = 0; i < MAX_SFX_STAGGERS; i++) {
        SfxStagger *s = &a->staggers[i];
        if (!s->active) continue;

        s->timer -= dt;
        while (s->timer <= 0.0f && s->remaining > 0) {
            audio_play_sfx(a, s->sfx);
            s->remaining--;
            float eff = s->interval;
            if (s->scale_by_anim) eff *= anim_speed;
            s->timer += eff;
        }
        if (s->remaining <= 0) s->active = false;
    }
}

void audio_update(AudioState *a, float dt, float anim_speed)
{
    /* Update playing music streams */
    if (a->current != MUSIC_NONE && a->track_loaded[a->current]) {
        UpdateMusicStream(a->tracks[a->current]);
    }
    if (a->previous != MUSIC_NONE && a->track_loaded[a->previous]) {
        UpdateMusicStream(a->tracks[a->previous]);
    }

    /* Crossfade logic */
    if (a->previous != MUSIC_NONE && a->fade_timer > 0.0f) {
        a->fade_timer -= dt;
        if (a->fade_timer <= 0.0f) a->fade_timer = 0.0f;

        float t = (FADE_DURATION > 0.0f)
                    ? (a->fade_timer / FADE_DURATION)
                    : 0.0f; /* 1→0 as fade progresses */
        float combined = a->music_vol * a->master_vol;

        /* Outgoing track fades out */
        if (a->track_loaded[a->previous]) {
            SetMusicVolume(a->tracks[a->previous], t * combined);
        }
        /* Incoming track fades in */
        if (a->current != MUSIC_NONE && a->track_loaded[a->current]) {
            SetMusicVolume(a->tracks[a->current], (1.0f - t) * combined);
        }

        /* Fade complete */
        if (a->fade_timer <= 0.0f) {
            if (a->track_loaded[a->previous]) {
                StopMusicStream(a->tracks[a->previous]);
            }
            a->previous = MUSIC_NONE;
            /* Set final volume */
            if (a->current != MUSIC_NONE && a->track_loaded[a->current]) {
                SetMusicVolume(a->tracks[a->current], combined);
            }
        }
    }

    /* Tick stagger sequences */
    update_staggers(a, dt, anim_speed);
}

void audio_set_music(AudioState *a, MusicContext ctx)
{
    if (ctx == a->current) return;

    /* Start fading out current track */
    if (a->current != MUSIC_NONE) {
        /* If already fading, stop the old outgoing track immediately */
        if (a->previous != MUSIC_NONE && a->track_loaded[a->previous]) {
            StopMusicStream(a->tracks[a->previous]);
        }
        a->previous = a->current;
    }

    a->current = ctx;
    a->fade_timer = FADE_DURATION;

    /* Start the new track */
    if (ctx != MUSIC_NONE && a->track_loaded[ctx]) {
        StopMusicStream(a->tracks[ctx]);
        PlayMusicStream(a->tracks[ctx]);
        SetMusicVolume(a->tracks[ctx], 0.0f); /* fade will bring it up */
    }
}

void audio_play_sfx(AudioState *a, SfxId id)
{
    if ((int)id < 0 || id >= SFX_COUNT) return;
    if (!a->sfx_loaded[id]) return;

    SetSoundVolume(a->sfx[id], a->sfx_vol * a->master_vol);
    PlaySound(a->sfx[id]);
}

void audio_start_stagger(AudioState *a, SfxId sfx, int count,
                         float base_interval, bool scale_anim)
{
    if (count <= 0) return;

    /* Find first inactive slot, fall back to slot 0 */
    int slot = 0;
    for (int i = 0; i < MAX_SFX_STAGGERS; i++) {
        if (!a->staggers[i].active) { slot = i; break; }
    }

    SfxStagger *s = &a->staggers[slot];
    s->active = true;
    s->sfx = sfx;
    s->remaining = count;
    s->interval = base_interval;
    s->scale_by_anim = scale_anim;
    s->timer = 0.0f;  /* fire first sound immediately on next update */
}

void audio_apply_settings(AudioState *a, const GameSettings *s)
{
    a->master_vol = s->master_volume;
    a->music_vol = s->music_volume;
    a->sfx_vol = s->sfx_volume;

    /* Update playing music volume if not mid-fade */
    if (a->previous == MUSIC_NONE &&
        a->current != MUSIC_NONE && a->track_loaded[a->current]) {
        SetMusicVolume(a->tracks[a->current], a->music_vol * a->master_vol);
    }
}
