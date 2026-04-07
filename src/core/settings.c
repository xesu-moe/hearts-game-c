/* ============================================================
 * @deps-implements: settings.h
 * @deps-requires: settings.h (GameSettings, auto_sort_received field),
 *                 vendor/cJSON.h, raylib.h, stdio.h, stdlib.h, string.h
 * @deps-last-changed: 2026-03-22 — Load/save auto_sort_received bool from JSON
 * ============================================================ */

#include "settings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#include "net/platform.h"
#include "vendor/cJSON.h"

#define SETTINGS_FILE "settings.json"

/* Build full path: <config_dir>/settings.json */
static bool settings_build_path(char *buf, size_t buflen)
{
    char dir[512];
    if (!net_config_dir(dir, sizeof(dir))) return false;
    int n = snprintf(buf, buflen, "%s/%s", dir, SETTINGS_FILE);
    return n > 0 && (size_t)n < buflen;
}
#define SESSION_TOKEN_LEN 32
#define SESSION_TOKEN_HEX_LEN (SESSION_TOKEN_LEN * 2)

static void hex_encode(const uint8_t *data, int len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < len; i++) {
        out[i * 2]     = hex[data[i] >> 4];
        out[i * 2 + 1] = hex[data[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static bool hex_decode(const char *hex_str, uint8_t *out, int out_len)
{
    for (int i = 0; i < out_len; i++) {
        unsigned int byte;
        if (sscanf(hex_str + i * 2, "%2x", &byte) != 1) return false;
        out[i] = (uint8_t)byte;
    }
    return true;
}

const Resolution RESOLUTIONS[RESOLUTION_COUNT] = {
    {1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160},
};

const int FPS_OPTIONS[FPS_OPTION_COUNT] = {30, 60, 120, 144, 165, 200, 240, 360};

void settings_default(GameSettings *s)
{
    s->window_mode      = WINDOW_MODE_WINDOWED;
    s->resolution_index = 0;  /* 1280x720 */
    s->fps_index        = 1;  /* 60 fps */
    s->anim_speed       = ANIM_SPEED_NORMAL;
    s->auto_sort_received = true;
    s->master_volume    = 1.0f;
    s->music_volume     = 1.0f;
    s->sfx_volume       = 1.0f;
    s->dirty            = false;
    memset(&s->reconnect, 0, sizeof(s->reconnect));
}

void settings_load(GameSettings *s)
{
    settings_default(s);

    char filepath[512];
    if (!settings_build_path(filepath, sizeof(filepath))) return;

    FILE *f = fopen(filepath, "rb");
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

    /* Reconnect info (optional) */
    cJSON *jrecon = cJSON_GetObjectItem(root, "reconnect");
    if (cJSON_IsObject(jrecon)) {
        cJSON *jip   = cJSON_GetObjectItem(jrecon, "server_ip");
        cJSON *jport = cJSON_GetObjectItem(jrecon, "server_port");
        cJSON *jrc   = cJSON_GetObjectItem(jrecon, "room_code");
        cJSON *jtok  = cJSON_GetObjectItem(jrecon, "session_token");
        if (cJSON_IsString(jip) && cJSON_IsNumber(jport) &&
            cJSON_IsString(jrc) && cJSON_IsString(jtok) &&
            strlen(jtok->valuestring) == SESSION_TOKEN_HEX_LEN) {
            strncpy(s->reconnect.server_ip, jip->valuestring,
                    sizeof(s->reconnect.server_ip) - 1);
            s->reconnect.server_port = (uint16_t)jport->valueint;
            strncpy(s->reconnect.room_code, jrc->valuestring,
                    sizeof(s->reconnect.room_code) - 1);
            if (hex_decode(jtok->valuestring, s->reconnect.session_token,
                           SESSION_TOKEN_LEN)) {
                s->reconnect.valid = true;
            }
        }
    }

    cJSON_Delete(root);

    /* Clamp volumes to valid range */
    if (s->master_volume < 0.0f) s->master_volume = 0.0f;
    if (s->master_volume > 1.0f) s->master_volume = 1.0f;
    if (s->music_volume < 0.0f) s->music_volume = 0.0f;
    if (s->music_volume > 1.0f) s->music_volume = 1.0f;
    if (s->sfx_volume < 0.0f) s->sfx_volume = 0.0f;
    if (s->sfx_volume > 1.0f) s->sfx_volume = 1.0f;

    /* Snap to nearest 0.1 to avoid ugly floating-point drift in settings.json */
    s->master_volume = roundf(s->master_volume * 10.0f) / 10.0f;
    s->music_volume  = roundf(s->music_volume  * 10.0f) / 10.0f;
    s->sfx_volume    = roundf(s->sfx_volume    * 10.0f) / 10.0f;

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

    cJSON_AddBoolToObject(root, "auto_sort_received", s->auto_sort_received);

    cJSON_AddNumberToObject(root, "master_volume", (double)s->master_volume);
    cJSON_AddNumberToObject(root, "music_volume", (double)s->music_volume);
    cJSON_AddNumberToObject(root, "sfx_volume", (double)s->sfx_volume);

    /* Reconnect info (only if valid) */
    if (s->reconnect.valid) {
        cJSON *jrecon = cJSON_AddObjectToObject(root, "reconnect");
        if (jrecon) {
            cJSON_AddStringToObject(jrecon, "server_ip", s->reconnect.server_ip);
            cJSON_AddNumberToObject(jrecon, "server_port", s->reconnect.server_port);
            cJSON_AddStringToObject(jrecon, "room_code", s->reconnect.room_code);
            char hex[SESSION_TOKEN_HEX_LEN + 1];
            hex_encode(s->reconnect.session_token, SESSION_TOKEN_LEN, hex);
            cJSON_AddStringToObject(jrecon, "session_token", hex);
        }
    }

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json_str) return;

    char filepath[512];
    if (settings_build_path(filepath, sizeof(filepath)) && net_ensure_config_dir()) {
        FILE *f = fopen(filepath, "w");
        if (f) {
            fputs(json_str, f);
            fclose(f);
        }
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
    SetTargetFPS(fps);
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


void settings_save_reconnect(const char *ip, uint16_t port,
                             const char *room_code, const uint8_t *token)
{
    GameSettings tmp;
    settings_load(&tmp);
    strncpy(tmp.reconnect.server_ip, ip, sizeof(tmp.reconnect.server_ip) - 1);
    tmp.reconnect.server_ip[sizeof(tmp.reconnect.server_ip) - 1] = '\0';
    tmp.reconnect.server_port = port;
    strncpy(tmp.reconnect.room_code, room_code, sizeof(tmp.reconnect.room_code) - 1);
    tmp.reconnect.room_code[sizeof(tmp.reconnect.room_code) - 1] = '\0';
    memcpy(tmp.reconnect.session_token, token, SESSION_TOKEN_LEN);
    tmp.reconnect.valid = true;
    settings_save(&tmp);
}

void settings_clear_reconnect(void)
{
    GameSettings tmp;
    settings_load(&tmp);
    if (!tmp.reconnect.valid) return;
    memset(&tmp.reconnect, 0, sizeof(tmp.reconnect));
    settings_save(&tmp);
}
