/* ============================================================
 * @deps-implements: settings_ui.h
 * @deps-requires: settings_ui.h, core/settings.h (GameSettings,
 *                 auto_sort_received field), render/render.h
 *                 (RenderState, SETTINGS_ROW_COUNT,
 *                 SETTINGS_ACTIVE_COUNT)
 * @deps-last-changed: 2026-03-22 — Added row 5 auto_sort_received toggle, volume rows shifted to 6,7,8
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
    COPY_SETTING(5, settings->auto_sort_received ? "On" : "Off");
    snprintf(rs->settings_value_bufs[6], sizeof(rs->settings_value_bufs[6]),
             "%d%%", (int)(settings->master_volume * 100.0f + 0.5f));
    snprintf(rs->settings_value_bufs[7], sizeof(rs->settings_value_bufs[7]),
             "%d%%", (int)(settings->music_volume * 100.0f + 0.5f));
    snprintf(rs->settings_value_bufs[8], sizeof(rs->settings_value_bufs[8]),
             "%d%%", (int)(settings->sfx_volume * 100.0f + 0.5f));
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
    case 5: /* auto_sort_received — immediate toggle */
        settings->auto_sort_received = !settings->auto_sort_received;
        break;
    case 6: { /* master_volume — immediate */
        float v = settings->master_volume + delta * 0.1f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        settings->master_volume = v;
        break;
    }
    case 7: { /* music_volume — immediate */
        float v = settings->music_volume + delta * 0.1f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        settings->music_volume = v;
        break;
    }
    case 8: { /* sfx_volume — immediate */
        float v = settings->sfx_volume + delta * 0.1f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        settings->sfx_volume = v;
        break;
    }
    default:
        return;
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
    settings_apply(settings);
    layout_recalculate(&rs->layout, GetScreenWidth(), GetScreenHeight());
    sync_settings_values(sui, settings, rs);
}
