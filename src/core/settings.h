#ifndef SETTINGS_H
#define SETTINGS_H

/* ============================================================
 * @deps-exports: WindowMode, AnimSpeed, AISpeed, Resolution,
 *                GameSettings, RESOLUTIONS[], FPS_OPTIONS[],
 *                settings_default(), settings_load(), settings_save(),
 *                settings_apply(), settings_anim_multiplier(),
 *                settings_ai_think_time(), settings_window_mode_name(),
 *                settings_resolution_name(), settings_fps_name(),
 *                settings_anim_speed_name(), settings_ai_speed_name()
 * @deps-requires: layout.h (LayoutConfig)
 * @deps-used-by: main.c, render.c
 * @deps-last-changed: 2026-03-16 — Initial creation
 * ============================================================ */

#include <stdbool.h>

/* Forward declaration to avoid circular include */
struct LayoutConfig;

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

typedef enum AISpeed {
    AI_SPEED_SLOW = 0,     /* 0.8s */
    AI_SPEED_NORMAL,       /* 0.4s */
    AI_SPEED_FAST,         /* 0.15s */
    AI_SPEED_INSTANT,      /* 0.0s */
    AI_SPEED_COUNT
} AISpeed;

typedef struct Resolution {
    int width;
    int height;
} Resolution;

#define RESOLUTION_COUNT 5
#define FPS_OPTION_COUNT 5

extern const Resolution RESOLUTIONS[RESOLUTION_COUNT];
extern const int FPS_OPTIONS[FPS_OPTION_COUNT];

typedef struct GameSettings {
    WindowMode window_mode;
    int        resolution_index; /* index into RESOLUTIONS[] */
    int        fps_index;        /* index into FPS_OPTIONS[] */

    AnimSpeed  anim_speed;
    AISpeed    ai_speed;

    /* Audio (reserved, zeroed) */
    float      master_volume;    /* 0.0-1.0 */
    float      music_volume;
    float      sfx_volume;

    bool       dirty;            /* unsaved changes */
} GameSettings;

void  settings_default(GameSettings *s);
void  settings_load(GameSettings *s);
void  settings_save(const GameSettings *s);
void  settings_apply(const GameSettings *s, struct LayoutConfig *layout);

float settings_anim_multiplier(AnimSpeed speed);
float settings_ai_think_time(AISpeed speed);

const char *settings_window_mode_name(WindowMode m);
const char *settings_resolution_name(int index);
const char *settings_fps_name(int index);
const char *settings_anim_speed_name(AnimSpeed s);
const char *settings_ai_speed_name(AISpeed s);

#endif /* SETTINGS_H */
