/* ============================================================
 * @deps-implements: (entry point)
 * @deps-requires: core/clock.h, core/game_state.h,
 *                 core/input.h, core/settings.h (g_settings for all
 *                 pass/settings functions), render/anim.h,
 *                 render/layout.h, render/render.h, render/card_render.h,
 *                 audio/audio.h, phase2/phase2_defs.h,
 *                 phase2/contract_logic.h, phase2/transmutation_logic.h,
 *                 game/ai.h, game/play_phase.h, game/pass_phase.h,
 *                 game/turn_flow.h, game/process_input.h, game/update.h,
 *                 game/settings_ui.h, game/info_sync.h,
 *                 game/phase_transitions.h, game/online_ui.h, game/login_ui.h,
 *                 net/client_net.h, net/protocol.h (net_input_cmd_is_relevant),
 *                 net/state_recv.h, net/lobby_client.h (lobby_client_cancel_create, lobby_client_cancel_join),
 *                 net/identity.h, raylib.h
 * @deps-last-changed: 2026-03-28 — Added client_net_peek_state() call before state_recv_apply()
 * ============================================================ */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "game/login_ui.h"
#include "game/online_ui.h"
#include "game/play_phase.h"
#include "game/pass_phase.h"
#include "game/turn_flow.h"
#include "game/process_input.h"
#include "game/update.h"
#include "game/settings_ui.h"
#include "game/info_sync.h"
#include "game/phase_transitions.h"
#include "net/client_net.h"
#include "net/identity.h"
#include "net/lobby_client.h"
#include "net/protocol.h"
#include "net/state_recv.h"
#include "core/debug_log.h"

#ifdef DEBUG
unsigned g_dbg_mask = DBG_ALL;
unsigned g_dbg_frame = 0;
#endif

/* ---- Constants ---- */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720
#define WINDOW_TITLE  "Hollow Hearts"
#define TARGET_FPS    60

#define DEFAULT_LOBBY_ADDR "127.0.0.1"
#define DEFAULT_LOBBY_PORT 7778

/* ---- CLI arg parsing ---- */
static void parse_args(int argc, char **argv,
                       char *lobby_addr, uint16_t *lobby_port)
{
    strncpy(lobby_addr, DEFAULT_LOBBY_ADDR, NET_ADDR_LEN - 1);
    *lobby_port = DEFAULT_LOBBY_PORT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--lobby-addr") == 0 && i + 1 < argc) {
            strncpy(lobby_addr, argv[++i], NET_ADDR_LEN - 1);
            lobby_addr[NET_ADDR_LEN - 1] = '\0';
        } else if (strcmp(argv[i], "--lobby-port") == 0 && i + 1 < argc) {
            *lobby_port = (uint16_t)atoi(argv[++i]);
        }
    }
}

/* ---- Entry point ---- */
int main(int argc, char **argv)
{
    /* Parse CLI args */
    char lobby_addr[NET_ADDR_LEN];
    uint16_t lobby_port;
    parse_args(argc, argv, lobby_addr, &lobby_port);

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

    /* Identity + lobby connection */
    Identity identity;
    identity_load_or_create(&identity);

    LoginUIState lui;
    login_ui_init(&lui);
    if (identity_load_username(lui.username_buf, sizeof(lui.username_buf))) {
        lui.username_len = (int)strlen(lui.username_buf);
        lui.has_stored_username = true;
        lui.show_username_input = false;
    }

    OnlineUIState oui;
    online_ui_init(&oui);

    lobby_client_init();
    lobby_client_connect(lobby_addr, lobby_port);

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
    rs.login_ui = &lui;
    rs.online_ui = &oui;

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

    dbg_init_from_env();

    while (!WindowShouldClose() && !quit_requested) {
#ifdef DEBUG
        g_dbg_frame++;
#endif
        bool online = (client_net_state() == CLIENT_NET_CONNECTED);
        rs.online = online;
        anim_set_speed(settings_anim_multiplier(g_settings.anim_speed));
        clock_update(&clk);
        client_net_update(clk.raw_dt);
        lobby_client_update(clk.raw_dt, &identity);

        /* Login screen text input */
        if (gs.phase == PHASE_LOGIN && lui.show_username_input)
            login_ui_update_text_input(&lui, clk.raw_dt);

        /* Lobby state machine transitions */
        if (gs.phase == PHASE_LOGIN) {
            LobbyClientState lcs = lobby_client_state();

            if (lcs == LOBBY_CONNECTED && !lui.awaiting_response) {
                if (lui.has_stored_username) {
                    /* Auto-login with stored username */
                    snprintf(lui.status_text, sizeof(lui.status_text),
                             "Logging in...");
                    lobby_client_login(lui.username_buf);
                    lui.awaiting_response = true;
                } else {
                    /* First launch — show username prompt */
                    lui.show_username_input = true;
                    lui.error_text[0] = '\0';
                }
            } else if (lcs == LOBBY_AUTHENTICATED) {
                if (lui.username_len >= 3)
                    identity_save_username(lui.username_buf);
                gs.phase = PHASE_MENU;
                rs.sync_needed = true;
                lui.awaiting_response = false;
            } else if (lcs == LOBBY_ERROR) {
                snprintf(lui.error_text, sizeof(lui.error_text), "%s",
                         lobby_client_error_msg());
                lui.show_username_input = false;
                lui.awaiting_response = false;
                /* Clear stored username flag so retry shows input field
                 * instead of re-attempting the same failed auto-login */
                lui.has_stored_username = false;
            }

            /* Handle Enter key as submit in username input */
            if (lui.show_username_input && !lui.awaiting_response &&
                IsKeyPressed(KEY_ENTER) && lui.username_len >= 3) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_LOGIN_SUBMIT,
                    .source_player = 0,
                });
            }
            /* Login commands processed below, after process_input */
        }

        /* Online menu state machine */
        if (gs.phase == PHASE_ONLINE_MENU) {
            /* Text input for room code join */
            if (oui.subphase == ONLINE_SUB_JOIN_INPUT)
                online_ui_update_text_input(&oui, clk.raw_dt);

            /* Check for room assignment from lobby */
            if (lobby_client_has_room_assignment()) {
                if (oui.subphase == ONLINE_SUB_MENU ||
                    oui.subphase == ONLINE_SUB_ERROR ||
                    oui.subphase == ONLINE_SUB_JOIN_INPUT) {
                    /* Stale assignment from a cancelled request — discard */
                    lobby_client_consume_room_assignment(
                        oui.server_addr, &oui.server_port,
                        oui.assigned_room_code, oui.assigned_auth_token);
                    oui.room_assigned = false;
                    printf("[main] Discarded stale room assignment\n");
                } else {
                    lobby_client_consume_room_assignment(
                        oui.server_addr, &oui.server_port,
                        oui.assigned_room_code, oui.assigned_auth_token);
                    oui.room_assigned = true;

                    if (oui.subphase == ONLINE_SUB_CREATE_WAITING) {
                        /* Show room code in waiting room */
                        strncpy(oui.created_room_code, oui.assigned_room_code,
                                NET_ROOM_CODE_LEN - 1);
                        oui.created_room_code[NET_ROOM_CODE_LEN - 1] = '\0';
                        /* Stay in CREATE_WAITING — game server will start
                         * when all 4 players join. We connect now. */
                        client_net_set_auth_token(oui.assigned_auth_token);
                        client_net_set_username(lobby_client_info()->username);
                        client_net_connect(oui.server_addr, oui.server_port,
                                           oui.assigned_room_code);
                    } else {
                        /* Join/Queue — show "Game Found!" then connect */
                        oui.subphase = ONLINE_SUB_MATCH_FOUND;
                        oui.match_found_timer = MATCH_FOUND_DURATION;
                    }
                }
            }

            /* Match found countdown */
            if (oui.subphase == ONLINE_SUB_MATCH_FOUND) {
                oui.match_found_timer -= clk.raw_dt;
                if (oui.match_found_timer <= 0.0f) {
                    oui.subphase = ONLINE_SUB_CONNECTING;
                    client_net_set_auth_token(oui.assigned_auth_token);
                    client_net_set_username(lobby_client_info()->username);
                    client_net_connect(oui.server_addr, oui.server_port,
                                       oui.assigned_room_code);
                }
            }

            /* Check game server connection */
            if (oui.subphase == ONLINE_SUB_CONNECTING) {
                ClientNetState cns = client_net_state();
                if (cns == CLIENT_NET_CONNECTED) {
                    /* Stay in online menu showing "waiting for game" */
                    oui.subphase = ONLINE_SUB_CONNECTED_WAITING;
                } else if (cns == CLIENT_NET_ERROR) {
                    snprintf(oui.error_text, sizeof(oui.error_text),
                             "Failed to connect to game server");
                    oui.subphase = ONLINE_SUB_ERROR;
                }
            }

            /* CREATE_WAITING: stay in waiting room, consume room status,
             * exit only when game starts (first state update from server) */
            if (oui.subphase == ONLINE_SUB_CREATE_WAITING) {
                ClientNetState cns = client_net_state();
                if (cns == CLIENT_NET_ERROR) {
                    snprintf(oui.error_text, sizeof(oui.error_text),
                             "Failed to connect to game server");
                    oui.subphase = ONLINE_SUB_ERROR;
                } else if (cns == CLIENT_NET_CONNECTED) {
                    /* Consume room status updates */
                    if (client_net_has_room_status()) {
                        NetMsgRoomStatus rs_msg;
                        client_net_consume_room_status(&rs_msg);
                        oui.player_count = rs_msg.player_count;
                        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                            memcpy(oui.player_names[i],
                                   rs_msg.player_names[i], NET_MAX_NAME_LEN);
                            oui.slot_is_ai[i] = (rs_msg.slot_is_ai[i] != 0);
                        }
                    }
                    /* Game started — server sent first state update.
                     * Exit the online UI; the state_recv apply block later
                     * in this frame will consume and set gs.phase. */
                    if (client_net_has_new_state()) {
                        oui.subphase = ONLINE_SUB_MENU;
                    }
                }
            }

            /* CONNECTED_WAITING: game server connected, waiting for game start */
            if (oui.subphase == ONLINE_SUB_CONNECTED_WAITING) {
                ClientNetState cns = client_net_state();
                if (cns == CLIENT_NET_ERROR) {
                    snprintf(oui.error_text, sizeof(oui.error_text),
                             "Lost connection to game server");
                    oui.subphase = ONLINE_SUB_ERROR;
                } else if (client_net_has_new_state()) {
                    /* First state update arrived — game is starting.
                     * Exit online UI; state_recv apply block sets gs.phase. */
                    oui.subphase = ONLINE_SUB_MENU;
                }
            }

            /* Check for lobby errors during room/queue operations */
            LobbyClientState lcs = lobby_client_state();
            if (oui.subphase != ONLINE_SUB_ERROR &&
                oui.subphase != ONLINE_SUB_MENU &&
                oui.subphase != ONLINE_SUB_MATCH_FOUND &&
                oui.subphase != ONLINE_SUB_CONNECTING &&
                oui.subphase != ONLINE_SUB_CONNECTED_WAITING) {
                if (oui.error_text[0] == '\0') {
                    /* Check if lobby reported an error */
                    const char *err = lobby_client_error_msg();
                    if (err[0] && lcs == LOBBY_AUTHENTICATED) {
                        /* Error was consumed, lobby went back to authenticated */
                        snprintf(oui.error_text, sizeof(oui.error_text),
                                 "%s", err);
                        oui.subphase = ONLINE_SUB_ERROR;
                    }
                }
            }

            /* Handle Enter key for join code submission */
            if (oui.subphase == ONLINE_SUB_JOIN_INPUT &&
                IsKeyPressed(KEY_ENTER) && oui.room_code_len == 4) {
                input_cmd_push((InputCmd){
                    .type = INPUT_CMD_ONLINE_JOIN,
                    .source_player = 0,
                });
            }
            /* Online menu commands processed below, after process_input */
        }

        process_input(&gs, &rs, &pps, &pls, &p2, flow.step);

        /* Route commands: online → server, offline → local */
        if (client_net_state() == CLIENT_NET_CONNECTED) {
            InputCmd cmd;
            InputCmd local_cmds[INPUT_CMD_QUEUE_CAPACITY];
            int local_count = 0;

            /* Drain queue, split into server-bound and local.
             * Dealer commands go to both server AND local queue
             * so the client gets immediate visual feedback. */
            while ((cmd = input_cmd_pop()).type != INPUT_CMD_NONE) {
                /* CONFIRM during SCORING is client-only (UI navigation) */
                bool scoring_confirm = (cmd.type == INPUT_CMD_CONFIRM &&
                                        gs.phase == PHASE_SCORING);
                if (net_input_cmd_is_relevant((uint8_t)cmd.type) &&
                    !scoring_confirm) {
                    cmd.source_player = client_net_seat();
                    client_net_send_cmd(&cmd);
                    /* Dealer commands also need local processing for UI */
                    if (cmd.type == INPUT_CMD_DEALER_DIR ||
                        cmd.type == INPUT_CMD_DEALER_AMT ||
                        cmd.type == INPUT_CMD_DEALER_CONFIRM) {
                        if (local_count < INPUT_CMD_QUEUE_CAPACITY)
                            local_cmds[local_count++] = cmd;
                    }
                } else {
                    if (local_count < INPUT_CMD_QUEUE_CAPACITY)
                        local_cmds[local_count++] = cmd;
                }
            }

            /* Re-push local-only commands for game_update */
            for (int i = 0; i < local_count; i++)
                input_cmd_push(local_cmds[i]);
        }

        /* Process login commands (after process_input populates the queue) */
        if (gs.phase == PHASE_LOGIN) {
            InputCmd cmd;
            InputCmd passthru[INPUT_CMD_QUEUE_CAPACITY];
            int passthru_count = 0;
            while ((cmd = input_cmd_pop()).type != INPUT_CMD_NONE) {
                if (cmd.type == INPUT_CMD_LOGIN_SUBMIT) {
                    if (lui.username_len < 3) {
                        snprintf(lui.error_text, sizeof(lui.error_text),
                                 "Username must be at least 3 characters");
                    } else if (lui.has_stored_username) {
                        lobby_client_login(lui.username_buf);
                        lui.awaiting_response = true;
                        snprintf(lui.status_text, sizeof(lui.status_text),
                                 "Logging in...");
                    } else {
                        lobby_client_register(lui.username_buf, &identity);
                        lui.awaiting_response = true;
                        lui.show_username_input = false;
                        snprintf(lui.status_text, sizeof(lui.status_text),
                                 "Registering...");
                    }
                } else if (cmd.type == INPUT_CMD_LOGIN_RETRY) {
                    lui.error_text[0] = '\0';
                    lui.awaiting_response = false;
                    snprintf(lui.status_text, sizeof(lui.status_text),
                             "Connecting...");
                    lobby_client_disconnect();
                    lobby_client_connect(lobby_addr, lobby_port);
                } else {
                    /* Re-push unrecognized commands for game_update */
                    if (passthru_count < INPUT_CMD_QUEUE_CAPACITY)
                        passthru[passthru_count++] = cmd;
                }
            }
            for (int i = 0; i < passthru_count; i++)
                input_cmd_push(passthru[i]);
        }

        /* Process online menu commands (after process_input populates the queue) */
        if (gs.phase == PHASE_ONLINE_MENU) {
            InputCmd cmd;
            InputCmd passthru[INPUT_CMD_QUEUE_CAPACITY];
            int passthru_count = 0;
            while ((cmd = input_cmd_pop()).type != INPUT_CMD_NONE) {
                if (cmd.type == INPUT_CMD_ONLINE_CREATE) {
                    lobby_client_create_room();
                    oui.subphase = ONLINE_SUB_CREATE_WAITING;
                    oui.player_count = 1;
                    oui.error_text[0] = '\0';
                } else if (cmd.type == INPUT_CMD_ONLINE_JOIN) {
                    if (oui.subphase == ONLINE_SUB_MENU) {
                        oui.subphase = ONLINE_SUB_JOIN_INPUT;
                        oui.room_code_len = 0;
                        oui.room_code_buf[0] = '\0';
                        oui.error_text[0] = '\0';
                    } else if (oui.subphase == ONLINE_SUB_JOIN_INPUT) {
                        if (oui.room_code_len == 4) {
                            lobby_client_join_room(oui.room_code_buf);
                            oui.subphase = ONLINE_SUB_JOIN_WAITING;
                            oui.error_text[0] = '\0';
                        }
                    }
                } else if (cmd.type == INPUT_CMD_ONLINE_QUICKMATCH) {
                    lobby_client_queue_matchmake();
                    oui.subphase = ONLINE_SUB_QUEUE_SEARCHING;
                    oui.error_text[0] = '\0';
                } else if (cmd.type == INPUT_CMD_ONLINE_ADD_AI) {
                    if (oui.subphase == ONLINE_SUB_CREATE_WAITING &&
                        client_net_state() == CLIENT_NET_CONNECTED) {
                        client_net_send_add_ai();
                    }
                } else if (cmd.type == INPUT_CMD_ONLINE_START) {
                    if (oui.subphase == ONLINE_SUB_CREATE_WAITING &&
                        client_net_state() == CLIENT_NET_CONNECTED) {
                        client_net_send_start_game();
                    }
                } else if (cmd.type == INPUT_CMD_ONLINE_CANCEL ||
                           cmd.type == INPUT_CMD_CANCEL) {
                    if (oui.subphase == ONLINE_SUB_QUEUE_SEARCHING) {
                        lobby_client_queue_cancel();
                    }
                    if (oui.subphase == ONLINE_SUB_CREATE_WAITING) {
                        lobby_client_cancel_create();
                    }
                    if (oui.subphase == ONLINE_SUB_JOIN_WAITING) {
                        lobby_client_cancel_join();
                    }
                    if (client_net_state() != CLIENT_NET_DISCONNECTED) {
                        client_net_disconnect();
                    }
                    if (oui.subphase == ONLINE_SUB_MENU ||
                        cmd.type == INPUT_CMD_CANCEL) {
                        gs.phase = PHASE_MENU;
                        rs.sync_needed = true;
                    }
                    online_ui_init(&oui);
                    oui.error_text[0] = '\0';
                } else if (cmd.type == INPUT_CMD_QUIT) {
                    quit_requested = true;
                } else {
                    /* Re-push unrecognized commands for game_update */
                    if (passthru_count < INPUT_CMD_QUEUE_CAPACITY)
                        passthru[passthru_count++] = cmd;
                }
            }
            for (int i = 0; i < passthru_count; i++)
                input_cmd_push(passthru[i]);
        }

        /* Step 9: Apply server state to local GameState + Phase2State.
         * Don't consume during trick animations — consuming with defer
         * permanently loses trick data since the state is popped from
         * the ring buffer. Leave it queued until flow is ready. */
        if (client_net_state() == CLIENT_NET_CONNECTED &&
            client_net_has_new_state()) {
            bool would_defer = (flow.step != FLOW_IDLE &&
                                flow.step != FLOW_WAITING_FOR_HUMAN);
            if (would_defer) goto skip_state_apply;

            /* Defer non-SCORING states while the scoring screen is active.
             * Allow consuming additional SCORING states (to get evaluated
             * contract data from the server's second SCORING broadcast). */
            if (gs.phase == PHASE_SCORING && online && !rs.scoring_screen_done) {
                const NetPlayerView *peek = client_net_peek_state();
                if (peek && peek->phase != PHASE_SCORING) {
                    goto skip_state_apply;
                }
            }

            /* Also defer if consuming would disrupt in-progress card animations.
             * Between card animations, flow returns to IDLE but we still have
             * saved trick cards to animate. Don't consume a state that would
             * change the phase or trick count — it would reset the visuals. */
            if (flow.has_saved_trick &&
                flow.prev_trick_count < flow.saved_trick.num_played) {
                const NetPlayerView *peek = client_net_peek_state();
                if (peek && (peek->phase != (uint8_t)gs.phase ||
                             peek->tricks_played != gs.tricks_played)) {
                    goto skip_state_apply;
                }
            }

            NetPlayerView view;
            client_net_consume_state(&view);

            DBG(DBG_STATE, "apply: defer=0 flow=%d phase=%d round=%d tricks=%d num_played=%d",
                flow.step, (int)view.phase, view.round_number,
                view.tricks_played, view.current_trick.num_played);
            state_recv_apply(&gs, &p2, &view, false);

            {
                PassSubphase new_sub = (PassSubphase)view.pass_subphase;
                if (new_sub != pps.subphase)
                    pps.timer = 0.0f; /* Reset countdown on subphase change */
                pps.subphase = new_sub;
            }

            /* Dealer UI: active only if we are the dealer and in dealer subphase */
            pps.dealer_ui_active =
                (view.dealer_seat >= 0 &&
                 view.dealer_seat == view.my_seat &&
                 pps.subphase == PASS_SUB_DEALER);

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

            DBG(DBG_SYNC, "sync_needed SET after state apply");
            rs.sync_needed = true;

            /* Sync pass phase UI from server state */
            pass_sync_online_ui(&pps, &gs, &rs, &p2);
        }
        skip_state_apply: ;

        /* Step 13: Display server error messages in chat log */
        {
            char errbuf[NET_MAX_CHAT_LEN];
            if (client_net_consume_error(errbuf, sizeof(errbuf))) {
                render_chat_log_push_color(&rs, errbuf, ORANGE);
            }
        }

        GamePhase phase_before_update = gs.phase;
        while (clk.accumulator >= FIXED_DT) {
            game_update(&gs, &rs, &p2, &pps, &pls, &lui, &oui, &sui,
                        &g_settings, &flow, FIXED_DT, &quit_requested);
            clk.accumulator -= FIXED_DT;
        }

        /* Reset flow if we just returned to menu mid-game (e.g. pause → return to menu) */
        if (is_ingame_phase(phase_before_update) && gs.phase == PHASE_MENU)
            flow_init(&flow);

        bool paused_ingame = rs.pause_state != PAUSE_INACTIVE &&
                             is_ingame_phase(gs.phase) && !online;

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

            /* Pass turn timer to render state for display */
            rs.turn_time_remaining = flow.turn_timer;

            /* Set interactive flag after flow_update */
            (void)0; /* placeholder: future dealer interactive state */
        }

        /* Music: single background track for all phases */
        audio_set_music(&audio, MUSIC_BACKGROUND);

        /* Apply audio settings each frame (cheap, keeps volumes in sync) */
        audio_apply_settings(&audio, &g_settings);

        /* Sync stats from lobby client into RenderState for stats screen */
        {
            const LobbyClientInfo *lci = lobby_client_info();
            if (lci && (lci->games_played > 0 || lci->elo_rating > 0)) {
                rs.stats_available = true;
                rs.stat_elo = lci->elo_rating;
                rs.stat_games_played = lci->games_played;
                rs.stat_games_won = lci->games_won;
            } else {
                rs.stats_available = false;
            }
        }

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

    lobby_client_shutdown();
    client_net_shutdown();
    audio_shutdown(&audio);
    card_render_transmute_shutdown();
    card_render_shutdown();
    if (rs.fog_shader_loaded) UnloadShader(rs.fog_shader);
    CloseWindow();
    return 0;
}
