/* ============================================================
 * @deps-implements: (entry point)
 * @deps-requires: core/clock.h, core/game_state.h, core/input.h,
 *                 core/settings.h, render/render.h, render/card_render.h,
 *                 phase2/phase2_defs.h, phase2/phase2_state.h,
 *                 phase2/contract_logic.h, phase2/transmutation_logic.h,
 *                 game/play_phase.h, game/pass_phase.h, game/turn_flow.h,
 *                 game/process_input.h, game/update.h, game/settings_ui.h,
 *                 game/info_sync.h, game/phase_transitions.h, raylib.h
 * @deps-last-changed: 2026-03-19 — Restructured: extracted modules into game/, core/ai, core/clock
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "core/clock.h"
#include "core/game_state.h"
#include "core/input.h"
#include "core/settings.h"
#include "render/card_render.h"
#include "render/render.h"
#include "phase2/phase2_defs.h"
#include "phase2/contract_logic.h"
#include "phase2/transmutation_logic.h"

#include "game/play_phase.h"
#include "game/pass_phase.h"
#include "game/turn_flow.h"
#include "game/process_input.h"
#include "game/update.h"
#include "game/settings_ui.h"
#include "game/info_sync.h"
#include "game/phase_transitions.h"

/* ---- Constants ---- */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720
#define WINDOW_TITLE  "Hollow Hearts"
#define TARGET_FPS    60

/* ---- Entry point ---- */
int main(void)
{
    /* Load settings before window creation to get resolution */
    GameSettings g_settings;
    settings_load(&g_settings);

    int init_w = RESOLUTIONS[g_settings.resolution_index].width;
    int init_h = RESOLUTIONS[g_settings.resolution_index].height;
    InitWindow(init_w, init_h, WINDOW_TITLE);

    GameClock clk;
    clock_init(&clk);

    GameState gs;
    game_state_init(&gs);
    input_init();

    Phase2State p2;
    phase2_defs_init();
    contract_state_init(&p2);
    p2.enabled = true;

    /* DEBUG: give player 0 a Black Joker for testing */
    transmute_inv_add(&p2.players[0].transmute_inv, 1);

    RenderState rs;
    render_init(&rs);

    /* Apply loaded settings (fullscreen, fps, layout) */
    settings_apply(&g_settings, &rs.layout);

    card_render_init();

    TurnFlow flow;
    flow_init(&flow);

    PassPhaseState pps = {
        .subphase = PASS_SUB_VENDETTA,
        .timer = 0.0f,
        .ai_vendetta_pending = false,
        .vendetta_ui_active = false,
    };

    PlayPhaseState pls = {
        .pending_transmutation = -1,
    };
    for (int ti = 0; ti < CARDS_PER_TRICK; ti++)
        pls.current_tti.transmutation_ids[ti] = -1;

    SettingsUIState sui = {
        .is_pending = false,
    };

    GamePhase prev_phase = gs.phase;
    bool prev_hearts_broken = false;
    bool quit_requested = false;

    while (!WindowShouldClose() && !quit_requested) {
        clock_update(&clk);
        process_input(&gs, &rs, &pps, &pls, &p2, flow.step);

        while (clk.accumulator >= FIXED_DT) {
            game_update(&gs, &rs, &p2, &pps, &pls, &sui, &g_settings,
                        FIXED_DT, &quit_requested);
            clk.accumulator -= FIXED_DT;
        }

        phase_transition_update(&gs, &rs, &p2, &pps, &pls, &flow,
                                &prev_phase, &prev_hearts_broken);

        /* Sync info panel */
        info_sync_update(&gs, &rs, &p2, &pls);

        /* Pass subphase timers (real time, UI deadlines) */
        pass_subphase_update(&pps, &gs, &rs, &p2, clk.raw_dt);

        /* Flow runs on raw_dt (real time) */
        flow_update(&flow, &gs, &rs, &p2, &g_settings, &pls, clk.raw_dt);

        /* Pass turn timer to render state for display */
        rs.turn_time_remaining = flow.turn_timer;

        /* Set interactive flag after flow_update */
        rs.vendetta_interactive = (flow.step == FLOW_WAITING_FOR_HUMAN);

        render_update(&gs, &rs, clk.raw_dt);

        /* Compute playability for human hand (transmute-aware) */
        info_sync_playability(&gs, &rs, &p2);

        /* Particle burst: hearts broken (must be after render_update) */
        phase_transition_post_render(&gs, &rs, &prev_hearts_broken);

        render_draw(&gs, &rs);
    }

    card_render_shutdown();
    CloseWindow();
    return 0;
}
