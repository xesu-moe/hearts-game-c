#ifndef SETTINGS_H
#define SETTINGS_H

/* ============================================================
 * @deps-exports: WindowMode, AnimSpeed, Resolution,
 *                ReconnectInfo, GameSettings (with reconnect field),
 *                RESOLUTIONS[], FPS_OPTIONS[], settings_default(),
 *                settings_load(), settings_save(), settings_apply(),
 *                settings_anim_multiplier(),
 *                settings_window_mode_name(), settings_resolution_name(),
 *                settings_fps_name(), settings_anim_speed_name(),
 *                settings_save_reconnect(), settings_clear_reconnect()
 * @deps-requires: stdint.h (uint16_t, uint8_t)
 * @deps-used-by: settings.c, render.c, turn_flow.h, turn_flow.c, update.h,
 *                update.c, settings_ui.h, settings_ui.c, pass_phase.h,
 *                pass_phase.c, main.c, audio/audio.h
 * @deps-last-changed: 2026-04-01 — Removed AISpeed enum, ai_speed field, and settings_ai_think_time/settings_ai_speed_name functions
 * ============================================================ */

#include <stdbool.h>
#include <stdint.h>

typedef enum WindowMode {
    WINDOW_MODE_WINDOWED = 0,
    WINDOW_MODE_FULLSCREEN,
    WINDOW_MODE_BORDERLESS,
    WINDOW_MODE_COUNT
} WindowMode;

typedef enum AnimSpeed {
    ANIM_SPEED_SLOW = 0,   /* 1.5x durations */
    ANIM_SPEED_NORMAL,     /* 1.0x */
    ANIM_SPEED_FAST,       /* 0.5x */
    ANIM_SPEED_COUNT
} AnimSpeed;

typedef struct Resolution {
    int width;
    int height;
} Resolution;

#define RESOLUTION_COUNT 5
#define FPS_OPTION_COUNT 5

extern const Resolution RESOLUTIONS[RESOLUTION_COUNT];
extern const int FPS_OPTIONS[FPS_OPTION_COUNT];

typedef struct ReconnectInfo {
    char     server_ip[64];     /* matches NET_ADDR_LEN */
    uint16_t server_port;
    char     room_code[8];      /* matches NET_ROOM_CODE_LEN */
    uint8_t  session_token[32]; /* matches NET_AUTH_TOKEN_LEN */
    bool     valid;
} ReconnectInfo;

typedef struct GameSettings {
    WindowMode window_mode;
    int        resolution_index; /* index into RESOLUTIONS[] */
    int        fps_index;        /* index into FPS_OPTIONS[] */

    AnimSpeed  anim_speed;
    bool       auto_sort_received; /* sort received cards into hand after pass */

    /* Audio (reserved, zeroed) */
    float      master_volume;    /* 0.0-1.0 */
    float      music_volume;
    float      sfx_volume;

    bool       dirty;            /* unsaved changes */

    /* Reconnect to previous game after disconnect / app restart */
    ReconnectInfo reconnect;
} GameSettings;

void  settings_default(GameSettings *s);
void  settings_load(GameSettings *s);
void  settings_save(const GameSettings *s);
void  settings_apply(const GameSettings *s);

float settings_anim_multiplier(AnimSpeed speed);

const char *settings_window_mode_name(WindowMode m);
const char *settings_resolution_name(int index);
const char *settings_fps_name(int index);
const char *settings_anim_speed_name(AnimSpeed s);

/* Persist / clear reconnect info in settings.json */
void settings_save_reconnect(const char *ip, uint16_t port,
                             const char *room_code, const uint8_t *token);
void settings_clear_reconnect(void);

#endif /* SETTINGS_H */
