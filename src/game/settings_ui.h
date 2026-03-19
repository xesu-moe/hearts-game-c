#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

/* ============================================================
 * @deps-exports: SettingsUIState, sync_settings_values(), setting_adjust(),
 *                apply_display_settings()
 * @deps-requires: core/settings.h
 * @deps-used-by: game/update.c, main.c
 * @deps-last-changed: 2026-03-19 — Extracted from main.c
 * ============================================================ */

#include <stdbool.h>

#include "core/settings.h"

/* Forward declarations */
typedef struct RenderState RenderState;

typedef struct SettingsUIState {
    GameSettings pending_display;
    bool         is_pending;
} SettingsUIState;

void sync_settings_values(SettingsUIState *sui, GameSettings *settings,
                          RenderState *rs);
void setting_adjust(SettingsUIState *sui, GameSettings *settings,
                    int setting_id, int delta, RenderState *rs);
void apply_display_settings(SettingsUIState *sui, GameSettings *settings,
                            RenderState *rs);

#endif /* SETTINGS_UI_H */
