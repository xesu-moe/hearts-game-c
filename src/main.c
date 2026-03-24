/* ============================================================
 * @deps-implements: (entry point)
 * @deps-requires: core/clock.h, core/game_state.h,
 *                 core/input.h, core/settings.h (g_settings for all
 *                 pass/settings functions), render/anim.h,
 *                 render/layout.h, render/render.h, render/card_render.h,
 *                 audio/audio.h, phase2/phase2_defs.h,
 *                 phase2/contract_logic.h, phase2/transmutation_logic.h,
 *                 game/ai.h, game/play_phase.h, game/pass_phase.h
 *                 (pass_subphase_update takes &g_settings),
 *                 game/turn_flow.h, game/process_input.h, game/update.h,
 *                 game/settings_ui.h, game/info_sync.h,
 *                 game/phase_transitions.h, net/client_net.h,
 *                 net/protocol.h (net_input_cmd_is_relevant),
 *                 net/state_recv.h (state_recv_apply), raylib.h
 * @deps-last-changed: 2026-03-23 — Step 10: Pass bool online to phase/flow/pass functions
 * ============================================================ */

#include <stdbool.h>

#include "raylib.h"

#include "game/ai.h"
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
#include "net/client_net.h"
#include "net/protocol.h"
#include "net/state_recv.h"

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
    SetExitKey(0);  /* Disable ESC auto-close — we handle ESC ourselves */

    GameClock clk;
    clock_init(&clk);

    GameState gs;
    game_state_init(&gs);
    input_init();
    client_net_init();

    Phase2State p2;
    phase2_defs_init();
    contract_state_init(&p2);
    p2.enabled = true;

#ifdef DEBUG
    /* Debug: give player 0 test transmutations */
    transmute_inv_add(&p2.players[0].transmute_inv,  8); /* Fog */
    transmute_inv_add(&p2.players[0].transmute_inv,  9); /* Mirror */
    transmute_inv_add(&p2.players[0].transmute_inv,  4); /* Gatherer */

#endif

    RenderState rs;
    render_init(&rs);

    /* Apply loaded settings (fullscreen, fps, layout) */
    settings_apply(&g_settings);
    layout_recalculate(&rs.layout, GetScreenWidth(), GetScreenHeight());

    AudioState audio;
    audio_init(&audio, &g_settings);

    card_render_init();
    card_render_transmute_init();

    TurnFlow flow;
    flow_init(&flow);

    PassPhaseState pps = {
        .subphase = PASS_SUB_DEALER,
        .timer = 0.0f,
    };

    PlayPhaseState pls = {
        .pending_transmutation = -1,
    };
    for (int ti = 0; ti < CARDS_PER_TRICK; ti++) {
        pls.current_tti.transmutation_ids[ti] = -1;
        pls.current_tti.transmuter_player[ti] = -1;
        pls.current_tti.resolved_effects[ti] = TEFFECT_NONE;
        pls.current_tti.fogged[ti] = false;
        pls.current_tti.fog_transmuter[ti] = -1;
    }

    SettingsUIState sui = {
        .is_pending = false,
    };

    GamePhase prev_phase = gs.phase;
    bool prev_hearts_broken = false;
    bool quit_requested = false;

    PassSubphase prev_subphase = pps.subphase;

    while (!WindowShouldClose() && !quit_requested) {
        bool online = (client_net_state() == CLIENT_NET_CONNECTED);
        anim_set_speed(settings_anim_multiplier(g_settings.anim_speed));
        clock_update(&clk);
        client_net_update(clk.raw_dt);
        process_input(&gs, &rs, &pps, &pls, &p2, flow.step);

        /* Route commands: online → server, offline → local */
        if (client_net_state() == CLIENT_NET_CONNECTED) {
            InputCmd cmd;
            InputCmd local_cmds[INPUT_CMD_QUEUE_CAPACITY];
            int local_count = 0;

            /* Drain queue, split into server-bound and local */
            while ((cmd = input_cmd_pop()).type != INPUT_CMD_NONE) {
                if (net_input_cmd_is_relevant((uint8_t)cmd.type)) {
                    cmd.source_player = client_net_seat();
                    client_net_send_cmd(&cmd);
                } else {
                    if (local_count < INPUT_CMD_QUEUE_CAPACITY)
                        local_cmds[local_count++] = cmd;
                }
            }

            /* Re-push local-only commands for game_update */
            for (int i = 0; i < local_count; i++)
                input_cmd_push(local_cmds[i]);
        }

        /* Step 9: Apply server state to local GameState + Phase2State */
        if (client_net_state() == CLIENT_NET_CONNECTED &&
            client_net_has_new_state()) {
            NetPlayerView view;
            client_net_consume_state(&view);

            state_recv_apply(&gs, &p2, &view);

            pps.subphase = (PassSubphase)view.pass_subphase;

            for (int i = 0; i < CARDS_PER_TRICK; i++) {
                pls.current_tti.transmutation_ids[i] =
                    view.trick_transmutes.transmutation_ids[i];
                int tp = view.trick_transmutes.transmuter_player[i];
                pls.current_tti.transmuter_player[i] =
                    (tp >= 0) ? (int)((tp - view.my_seat + NUM_PLAYERS)
                                      % NUM_PLAYERS) : -1;
                pls.current_tti.resolved_effects[i] =
                    (TransmuteEffect)view.trick_transmutes.resolved_effects[i];
                pls.current_tti.fogged[i] =
                    view.trick_transmutes.fogged[i];
                pls.current_tti.fog_transmuter[i] = -1;
            }

            rs.turn_time_remaining = view.turn_timer;
            rs.sync_needed = true;
        }

        /* Step 13: Display server error messages in chat log */
        {
            char errbuf[NET_MAX_CHAT_LEN];
            if (client_net_consume_error(errbuf, sizeof(errbuf))) {
                render_chat_log_push_color(&rs, errbuf, ORANGE);
            }
        }

        GamePhase phase_before_update = gs.phase;
        while (clk.accumulator >= FIXED_DT) {
            game_update(&gs, &rs, &p2, &pps, &pls, &sui, &g_settings,
                        &flow, FIXED_DT, &quit_requested);
            clk.accumulator -= FIXED_DT;
        }

        /* Reset flow if we just returned to menu mid-game (e.g. pause → return to menu) */
        if (is_ingame_phase(phase_before_update) && gs.phase == PHASE_MENU)
            flow_init(&flow);

        bool paused_ingame = rs.pause_state != PAUSE_INACTIVE &&
                             is_ingame_phase(gs.phase);

        audio_update(&audio, clk.raw_dt, anim_get_speed());

        if (!paused_ingame) {
            phase_transition_update(&gs, &rs, &p2, &pps, &pls, &flow,
                                    &prev_phase, &prev_hearts_broken,
                                    online);

            /* SFX: deal — one sound per card at stagger rate */
            if (gs.phase == PHASE_DEALING && phase_before_update != PHASE_DEALING)
                audio_start_stagger(&audio, SFX_CARD_DEAL, DECK_SIZE,
                                    ANIM_DEAL_CARD_STAGGER, true);

            /* SFX: hearts broken */
            if (gs.hearts_broken && !prev_hearts_broken)
                audio_play_sfx(&audio, SFX_HEARTS_BROKEN);

            /* Sync info panel */
            info_sync_update(&gs, &rs, &p2, &pls);

            /* Pass subphase timers (real time, UI deadlines) */
            pass_subphase_update(&pps, &gs, &rs, &p2, &g_settings,
                                clk.raw_dt, online);

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

            /* Flow runs on raw_dt (real time) */
            flow_update(&flow, &gs, &rs, &p2, &g_settings, &pls,
                       clk.raw_dt, online);

            /* Pass turn timer to render state for display.
             * Online: server sets this via state_recv_apply. */
            if (!online)
                rs.turn_time_remaining = flow.turn_timer;

            /* Set interactive flag after flow_update */
            (void)0; /* placeholder: future dealer interactive state */
        }

        /* Music: single background track for all phases */
        audio_set_music(&audio, MUSIC_BACKGROUND);

        /* Apply audio settings each frame (cheap, keeps volumes in sync) */
        audio_apply_settings(&audio, &g_settings);

        render_update(&gs, &rs, clk.raw_dt);

        if (!paused_ingame) {
            /* SFX: score tick */
            if (rs.score_tick_pending) {
                audio_play_sfx(&audio, SFX_SCORE_TICK);
                rs.score_tick_pending = false;
            }

            /* Compute playability for human hand (transmute-aware) */
            info_sync_playability(&gs, &rs, &p2);

            /* Particle burst: hearts broken (must be after render_update) */
            phase_transition_post_render(&gs, &rs, &prev_hearts_broken);
        }

        render_draw(&gs, &rs);
    }

    client_net_shutdown();
    audio_shutdown(&audio);
    card_render_transmute_shutdown();
    card_render_shutdown();
    if (rs.fog_shader_loaded) UnloadShader(rs.fog_shader);
    CloseWindow();
    return 0;
}
