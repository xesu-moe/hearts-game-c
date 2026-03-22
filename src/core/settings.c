/* ============================================================
 * @deps-implements: settings.h
 * @deps-requires: settings.h (GameSettings, auto_sort_received field),
 *                 vendor/cJSON.h, raylib.h, stdio.h, stdlib.h, string.h
 * @deps-last-changed: 2026-03-22 — Load/save auto_sort_received bool from JSON
 * ============================================================ */

#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#include "vendor/cJSON.h"

#define SETTINGS_FILE "settings.json"

const Resolution RESOLUTIONS[RESOLUTION_COUNT] = {
    {1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440},
};

const int FPS_OPTIONS[FPS_OPTION_COUNT] = {30, 60, 120, 144, 0}; /* 0 = uncapped */

void settings_default(GameSettings *s)
{
    s->window_mode      = WINDOW_MODE_WINDOWED;
    s->resolution_index = 0;  /* 1280x720 */
    s->fps_index        = 1;  /* 60 fps */
    s->anim_speed       = ANIM_SPEED_NORMAL;
    s->ai_speed         = AI_SPEED_NORMAL;
    s->auto_sort_received = true;
    s->master_volume    = 1.0f;
    s->music_volume     = 1.0f;
    s->sfx_volume       = 1.0f;
    s->dirty            = false;
}

void settings_load(GameSettings *s)
{
    settings_default(s);

    FILE *f = fopen(SETTINGS_FILE, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || len > 65536) {
        fclose(f);
        return;
    }

    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return;
    }

    size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[read] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;

    /* Window mode */
    cJSON *jmode = cJSON_GetObjectItem(root, "window_mode");
    if (cJSON_IsString(jmode)) {
        if (strcmp(jmode->valuestring, "fullscreen") == 0)
            s->window_mode = WINDOW_MODE_FULLSCREEN;
        else if (strcmp(jmode->valuestring, "borderless") == 0)
            s->window_mode = WINDOW_MODE_BORDERLESS;
        else
            s->window_mode = WINDOW_MODE_WINDOWED;
    }

    /* Resolution index */
    cJSON *jres = cJSON_GetObjectItem(root, "resolution_index");
    if (cJSON_IsNumber(jres)) {
        int idx = jres->valueint;
        if (idx >= 0 && idx < RESOLUTION_COUNT) s->resolution_index = idx;
    }

    /* FPS index */
    cJSON *jfps = cJSON_GetObjectItem(root, "fps_index");
    if (cJSON_IsNumber(jfps)) {
        int idx = jfps->valueint;
        if (idx >= 0 && idx < FPS_OPTION_COUNT) s->fps_index = idx;
    }

    /* Anim speed */
    cJSON *janim = cJSON_GetObjectItem(root, "anim_speed");
    if (cJSON_IsString(janim)) {
        if (strcmp(janim->valuestring, "slow") == 0)
            s->anim_speed = ANIM_SPEED_SLOW;
        else if (strcmp(janim->valuestring, "fast") == 0)
            s->anim_speed = ANIM_SPEED_FAST;
        else
            s->anim_speed = ANIM_SPEED_NORMAL;
    }

    /* AI speed */
    cJSON *jai = cJSON_GetObjectItem(root, "ai_speed");
    if (cJSON_IsString(jai)) {
        if (strcmp(jai->valuestring, "slow") == 0)
            s->ai_speed = AI_SPEED_SLOW;
        else if (strcmp(jai->valuestring, "fast") == 0)
            s->ai_speed = AI_SPEED_FAST;
        else if (strcmp(jai->valuestring, "instant") == 0)
            s->ai_speed = AI_SPEED_INSTANT;
        else
            s->ai_speed = AI_SPEED_NORMAL;
    }

    /* Auto-sort received */
    cJSON *jasr = cJSON_GetObjectItem(root, "auto_sort_received");
    if (cJSON_IsBool(jasr)) s->auto_sort_received = cJSON_IsTrue(jasr);

    /* Volumes */
    cJSON *jmv = cJSON_GetObjectItem(root, "master_volume");
    if (cJSON_IsNumber(jmv)) s->master_volume = (float)jmv->valuedouble;

    cJSON *jmuv = cJSON_GetObjectItem(root, "music_volume");
    if (cJSON_IsNumber(jmuv)) s->music_volume = (float)jmuv->valuedouble;

    cJSON *jsv = cJSON_GetObjectItem(root, "sfx_volume");
    if (cJSON_IsNumber(jsv)) s->sfx_volume = (float)jsv->valuedouble;

    cJSON_Delete(root);

    /* Clamp volumes to valid range */
    if (s->master_volume < 0.0f) s->master_volume = 0.0f;
    if (s->master_volume > 1.0f) s->master_volume = 1.0f;
    if (s->music_volume < 0.0f) s->music_volume = 0.0f;
    if (s->music_volume > 1.0f) s->music_volume = 1.0f;
    if (s->sfx_volume < 0.0f) s->sfx_volume = 0.0f;
    if (s->sfx_volume > 1.0f) s->sfx_volume = 1.0f;

    s->dirty = false;
}

void settings_save(const GameSettings *s)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    /* Window mode as string */
    const char *mode_str = "windowed";
    if (s->window_mode == WINDOW_MODE_FULLSCREEN) mode_str = "fullscreen";
    else if (s->window_mode == WINDOW_MODE_BORDERLESS) mode_str = "borderless";
    cJSON_AddStringToObject(root, "window_mode", mode_str);

    cJSON_AddNumberToObject(root, "resolution_index", s->resolution_index);
    cJSON_AddNumberToObject(root, "fps_index", s->fps_index);

    /* Anim speed as string */
    const char *anim_str = "normal";
    if (s->anim_speed == ANIM_SPEED_SLOW) anim_str = "slow";
    else if (s->anim_speed == ANIM_SPEED_FAST) anim_str = "fast";
    cJSON_AddStringToObject(root, "anim_speed", anim_str);

    /* AI speed as string */
    const char *ai_str = "normal";
    if (s->ai_speed == AI_SPEED_SLOW) ai_str = "slow";
    else if (s->ai_speed == AI_SPEED_FAST) ai_str = "fast";
    else if (s->ai_speed == AI_SPEED_INSTANT) ai_str = "instant";
    cJSON_AddStringToObject(root, "ai_speed", ai_str);

    cJSON_AddBoolToObject(root, "auto_sort_received", s->auto_sort_received);

    cJSON_AddNumberToObject(root, "master_volume", (double)s->master_volume);
    cJSON_AddNumberToObject(root, "music_volume", (double)s->music_volume);
    cJSON_AddNumberToObject(root, "sfx_volume", (double)s->sfx_volume);

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json_str) return;

    FILE *f = fopen(SETTINGS_FILE, "w");
    if (f) {
        fputs(json_str, f);
        fclose(f);
    }

    free(json_str);
}

void settings_apply(const GameSettings *s)
{
    /* Window mode */
    bool currently_fullscreen = IsWindowFullscreen();
    bool want_fullscreen = (s->window_mode == WINDOW_MODE_FULLSCREEN);
    bool want_borderless = (s->window_mode == WINDOW_MODE_BORDERLESS);

    if (want_borderless) {
        if (currently_fullscreen) ToggleFullscreen();
        SetWindowState(FLAG_WINDOW_UNDECORATED);
        int monitor = GetCurrentMonitor();
        SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
        SetWindowPosition(0, 0);
    } else if (want_fullscreen) {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
        if (!currently_fullscreen) ToggleFullscreen();
    } else {
        /* Windowed */
        if (currently_fullscreen) ToggleFullscreen();
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
        int w = RESOLUTIONS[s->resolution_index].width;
        int h = RESOLUTIONS[s->resolution_index].height;
        SetWindowSize(w, h);
        /* Center window on monitor (e.g. after returning from borderless) */
        int monitor = GetCurrentMonitor();
        int mx = (GetMonitorWidth(monitor) - w) / 2;
        int my = (GetMonitorHeight(monitor) - h) / 2;
        SetWindowPosition(mx, my);
    }

    /* FPS */
    int fps = FPS_OPTIONS[s->fps_index];
    SetTargetFPS(fps); /* 0 = uncapped */
}

float settings_anim_multiplier(AnimSpeed speed)
{
    switch (speed) {
    case ANIM_SPEED_SLOW:   return 1.5f;
    case ANIM_SPEED_NORMAL: return 1.0f;
    case ANIM_SPEED_FAST:   return 0.5f;
    default:                return 1.0f;
    }
}

float settings_ai_think_time(AISpeed speed)
{
    switch (speed) {
    case AI_SPEED_SLOW:    return 0.8f;
    case AI_SPEED_NORMAL:  return 0.4f;
    case AI_SPEED_FAST:    return 0.15f;
    case AI_SPEED_INSTANT: return 0.0f;
    default:               return 0.4f;
    }
}

const char *settings_window_mode_name(WindowMode m)
{
    switch (m) {
    case WINDOW_MODE_WINDOWED:   return "Windowed";
    case WINDOW_MODE_FULLSCREEN: return "Fullscreen";
    case WINDOW_MODE_BORDERLESS: return "Borderless";
    default:                     return "Unknown";
    }
}

const char *settings_resolution_name(int index)
{
    static char buf[32];
    if (index < 0 || index >= RESOLUTION_COUNT) return "Unknown";
    snprintf(buf, sizeof(buf), "%dx%d",
             RESOLUTIONS[index].width, RESOLUTIONS[index].height);
    return buf;
}

const char *settings_fps_name(int index)
{
    if (index < 0 || index >= FPS_OPTION_COUNT) return "Unknown";
    if (FPS_OPTIONS[index] == 0) return "Uncapped";
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d", FPS_OPTIONS[index]);
    return buf;
}

const char *settings_anim_speed_name(AnimSpeed s)
{
    switch (s) {
    case ANIM_SPEED_SLOW:   return "Slow";
    case ANIM_SPEED_NORMAL: return "Normal";
    case ANIM_SPEED_FAST:   return "Fast";
    default:                return "Unknown";
    }
}

const char *settings_ai_speed_name(AISpeed s)
{
    switch (s) {
    case AI_SPEED_SLOW:    return "Slow";
    case AI_SPEED_NORMAL:  return "Normal";
    case AI_SPEED_FAST:    return "Fast";
    case AI_SPEED_INSTANT: return "Instant";
    default:               return "Unknown";
    }
}
