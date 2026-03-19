/* ============================================================
 * @deps-implements: (entry point)
 * @deps-requires: core/clock.h, core/game_state.h (game_state_advance_scoring),
 *                 core/input.h, core/settings.h,
 *                 render/anim.h (anim_set_speed, ANIM_DEAL_CARD_STAGGER),
 *                 render/render.h (RenderState.score_advance_pending/tick_pending),
 *                 render/card_render.h, audio/audio.h (SFX_SCORE_TICK),
 *                 phase2/phase2_defs.h, game modules, raylib.h
 * @deps-last-changed: 2026-03-19 — Consume score_advance_pending flag; manage RT lifecycle (load/unload score_contracts_rt); call game_state_advance_scoring()
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "core/ai.h"
#include "core/clock.h"
#include "core/game_state.h"
#include "core/input.h"
#include "core/settings.h"
#include "render/anim.h"
#include "render/card_render.h"
#include "render/render.h"
#include "phase2/phase2_defs.h"
#include "phase2/contract_logic.h"
#include "phase2/transmutation_logic.h"

#include "audio/audio.h"
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

    AudioState audio;
    audio_init(&audio, &g_settings);

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

    PassSubphase prev_subphase = pps.subphase;

    while (!WindowShouldClose() && !quit_requested) {
        anim_set_speed(settings_anim_multiplier(g_settings.anim_speed));
        clock_update(&clk);
        process_input(&gs, &rs, &pps, &pls, &p2, flow.step);

        /* DEBUG: F5 = skip to last trick */
        if (IsKeyPressed(KEY_F5) && gs.phase == PHASE_PLAYING) {
            while (gs.phase == PHASE_PLAYING && gs.tricks_played < 12) {
                if (gs.current_trick.num_played >= CARDS_PER_TRICK) {
                    game_state_complete_trick(&gs);
                    if (gs.phase != PHASE_PLAYING) break;
                }
                int current = game_state_current_player(&gs);
                if (current < 0) {
                    game_state_complete_trick(&gs);
                    if (gs.phase != PHASE_PLAYING) break;
                    continue;
                }
                ai_play_card(&gs, &rs, &p2, &pls, current);
            }
            if (gs.phase == PHASE_PLAYING &&
                gs.current_trick.num_played >= CARDS_PER_TRICK) {
                game_state_complete_trick(&gs);
            }
            rs.sync_needed = true;
            rs.pile_card_count = 0;
            flow_init(&flow);
        }

        GamePhase phase_before_update = gs.phase;
        while (clk.accumulator >= FIXED_DT) {
            game_update(&gs, &rs, &p2, &pps, &pls, &sui, &g_settings,
                        FIXED_DT, &quit_requested);
            clk.accumulator -= FIXED_DT;
        }

        audio_update(&audio, clk.raw_dt);

        phase_transition_update(&gs, &rs, &p2, &pps, &pls, &flow,
                                &prev_phase, &prev_hearts_broken);

        /* SFX: deal — one sound per card at stagger rate */
        if (gs.phase == PHASE_DEALING && phase_before_update != PHASE_DEALING)
            audio_start_stagger(&audio, SFX_CARD_DEAL, DECK_SIZE,
                                ANIM_DEAL_CARD_STAGGER, true);

        /* Music: single background track for all phases */
        audio_set_music(&audio, MUSIC_BACKGROUND);

        /* SFX: hearts broken */
        if (gs.hearts_broken && !prev_hearts_broken)
            audio_play_sfx(&audio, SFX_HEARTS_BROKEN);

        /* Sync info panel */
        info_sync_update(&gs, &rs, &p2, &pls);

        /* Pass subphase timers (real time, UI deadlines) */
        pass_subphase_update(&pps, &gs, &rs, &p2, clk.raw_dt);

        /* SFX: pass toss — one sound per card at stagger rate */
        if (pps.subphase == PASS_SUB_TOSS_ANIM &&
            prev_subphase != PASS_SUB_TOSS_ANIM)
            audio_start_stagger(&audio, SFX_CARD_PLAY, rs.pass_staged_count,
                                PASS_TOSS_STAGGER, true);
        prev_subphase = pps.subphase;

        /* Consume SFX flags from PlayPhaseState */
        if (pls.card_played_sfx) {
            audio_play_sfx(&audio, SFX_CARD_PLAY);
            pls.card_played_sfx = false;
        }
        if (pls.transmute_sfx) {
            audio_play_sfx(&audio, SFX_TRANSMUTE);
            pls.transmute_sfx = false;
        }

        /* Apply audio settings each frame (cheap, keeps volumes in sync) */
        audio_apply_settings(&audio, &g_settings);

        /* Flow runs on raw_dt (real time) */
        flow_update(&flow, &gs, &rs, &p2, &g_settings, &pls, clk.raw_dt);

        /* Pass turn timer to render state for display */
        rs.turn_time_remaining = flow.turn_timer;

        /* Set interactive flag after flow_update */
        rs.vendetta_interactive = (flow.step == FLOW_WAITING_FOR_HUMAN);

        render_update(&gs, &rs, clk.raw_dt);

        /* SFX: score tick */
        if (rs.score_tick_pending) {
            audio_play_sfx(&audio, SFX_SCORE_TICK);
            rs.score_tick_pending = false;
        }

        /* Compute playability for human hand (transmute-aware) */
        info_sync_playability(&gs, &rs, &p2);

        /* Particle burst: hearts broken (must be after render_update) */
        phase_transition_post_render(&gs, &rs, &prev_hearts_broken);

        render_draw(&gs, &rs);
    }

    audio_shutdown(&audio);
    card_render_shutdown();
    CloseWindow();
    return 0;
}
