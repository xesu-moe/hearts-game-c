/* ============================================================
 * @deps-implements: settings_ui.h
 * @deps-requires: settings_ui.h, core/settings.h, render/render.h
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include "settings_ui.h"

#include <stdio.h>

#include "render/render.h"

void sync_settings_values(SettingsUIState *sui, GameSettings *settings,
                          RenderState *rs)
{
    /* Show pending display values if unapplied, otherwise show active */
    WindowMode show_wm = sui->is_pending ? sui->pending_display.window_mode
                                         : settings->window_mode;
    int show_res = sui->is_pending ? sui->pending_display.resolution_index
                                   : settings->resolution_index;
    int show_fps = sui->is_pending ? sui->pending_display.fps_index
                                   : settings->fps_index;

    /* Copy into owned buffers to avoid stale static-buffer pointers */
    #define COPY_SETTING(i, str) snprintf(rs->settings_value_bufs[i], \
        sizeof(rs->settings_value_bufs[i]), "%s", (str))
    COPY_SETTING(0, settings_window_mode_name(show_wm));
    COPY_SETTING(1, settings_resolution_name(show_res));
    COPY_SETTING(2, settings_fps_name(show_fps));
    COPY_SETTING(3, settings_anim_speed_name(settings->anim_speed));
    COPY_SETTING(4, settings_ai_speed_name(settings->ai_speed));
    COPY_SETTING(5, "(No Audio)");
    COPY_SETTING(6, "(No Audio)");
    COPY_SETTING(7, "(No Audio)");
    #undef COPY_SETTING
}

void setting_adjust(SettingsUIState *sui, GameSettings *settings,
                    int setting_id, int delta, RenderState *rs)
{
    switch (setting_id) {
    case 0: /* window_mode — deferred until Apply */
        if (!sui->is_pending) {
            sui->pending_display.window_mode = settings->window_mode;
            sui->pending_display.resolution_index = settings->resolution_index;
            sui->pending_display.fps_index = settings->fps_index;
        }
        sui->pending_display.window_mode = (WindowMode)(
            ((int)sui->pending_display.window_mode + delta +
             WINDOW_MODE_COUNT) % WINDOW_MODE_COUNT);
        sui->is_pending = true;
        sync_settings_values(sui, settings, rs);
        return;
    case 1: /* resolution — deferred until Apply */
        if (!sui->is_pending) {
            sui->pending_display.window_mode = settings->window_mode;
            sui->pending_display.resolution_index = settings->resolution_index;
            sui->pending_display.fps_index = settings->fps_index;
        }
        sui->pending_display.resolution_index =
            (sui->pending_display.resolution_index + delta +
             RESOLUTION_COUNT) % RESOLUTION_COUNT;
        sui->is_pending = true;
        sync_settings_values(sui, settings, rs);
        return;
    case 2: /* fps — deferred until Apply */
        if (!sui->is_pending) {
            sui->pending_display.window_mode = settings->window_mode;
            sui->pending_display.resolution_index = settings->resolution_index;
            sui->pending_display.fps_index = settings->fps_index;
        }
        sui->pending_display.fps_index =
            (sui->pending_display.fps_index + delta +
             FPS_OPTION_COUNT) % FPS_OPTION_COUNT;
        sui->is_pending = true;
        sync_settings_values(sui, settings, rs);
        return;
    case 3: /* anim_speed — immediate */
        settings->anim_speed = (AnimSpeed)(((int)settings->anim_speed + delta +
                                            ANIM_SPEED_COUNT) % ANIM_SPEED_COUNT);
        break;
    case 4: /* ai_speed — immediate */
        settings->ai_speed = (AISpeed)(((int)settings->ai_speed + delta +
                                        AI_SPEED_COUNT) % AI_SPEED_COUNT);
        break;
    default:
        return; /* audio rows — skip */
    }

    settings->dirty = true;
    sync_settings_values(sui, settings, rs);
}

void apply_display_settings(SettingsUIState *sui, GameSettings *settings,
                            RenderState *rs)
{
    if (!sui->is_pending) return;
    settings->window_mode = sui->pending_display.window_mode;
    settings->resolution_index = sui->pending_display.resolution_index;
    settings->fps_index = sui->pending_display.fps_index;
    settings->dirty = true;
    sui->is_pending = false;
    settings_apply(settings, &rs->layout);
    sync_settings_values(sui, settings, rs);
}
