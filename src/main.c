/* ============================================================
 * @deps-implements: (entry point)
 * @deps-requires: core/clock.h, core/game_state.h (GameState, GamePhase, PHASE_SETTINGS),
 *                 core/input.h, core/settings.h (g_settings),
 *                 render/anim.h, render/layout.h, render/render.h (deal_anim),
 *                 render/card_render.h, audio/audio.h, phase2/phase2_defs.h,
 *                 phase2/contract_logic.h, phase2/transmutation_logic.h,
 *                 game/play_phase.h, game/pass_phase.h, game/turn_flow.h,
 *                 game/process_input.h, game/update.h, game/settings_ui.h,
 *                 game/info_sync.h, game/phase_transitions.h, game/online_ui.h,
 *                 game/login_ui.h, net/client_net.h, net/protocol.h,
 *                 net/state_recv.h, net/lobby_client.h, net/identity.h, raylib.h
 * @deps-last-changed: 2026-04-01
 * ============================================================ */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/platform.h"
#include "raylib.h"

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

/* ---- Constants ---- */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720
#define WINDOW_TITLE  "Hollow Hearts"
#define TARGET_FPS    60

#define DEFAULT_LOBBY_ADDR "127.0.0.1"
#define DEFAULT_LOBBY_PORT 7778

/* ---- DNS resolution ---- */
/* Resolves hostname in-place to dotted-decimal IP. Returns 0 on success. */
static int resolve_hostname(char *host_buf, size_t buf_len)
{
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    if (getaddrinfo(host_buf, NULL, &hints, &res) != 0 || !res) {
        if (res) freeaddrinfo(res);
        return -1;
    }
    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, host_buf, (socklen_t)buf_len);
    freeaddrinfo(res);
    return 0;
}

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
    net_platform_init();

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
    if (!GetWindowHandle()) {
        fprintf(stderr, "ERROR: Failed to create window (no valid GPU/GL context). "
                "Try setting LIBGL_ALWAYS_SOFTWARE=1 or check your graphics drivers.\n");
        return 1;
    }
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

    /* Resolve lobby hostname (may block briefly for DNS) */
    BeginDrawing();
    ClearBackground(BLACK);
    DrawText("Connecting to lobby...", init_w / 2 - 120, init_h / 2, 20, WHITE);
    EndDrawing();

    if (resolve_hostname(lobby_addr, sizeof(lobby_addr)) != 0) {
        TraceLog(LOG_ERROR, "Failed to resolve lobby address: %s", lobby_addr);
        /* Fall through — lobby_client_connect will fail and UI shows error */
    }

    lobby_client_init();
    lobby_client_connect(lobby_addr, lobby_port);

    Phase2State p2;
    phase2_defs_init();
    contract_state_init(&p2);
    p2.enabled = true;


    RenderState rs;
    render_init(&rs);
    rs.login_ui = &lui;
    rs.online_ui = &oui;
    rs.friend_panel = &oui.friend_panel;

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
        .server_trick_winner = -1,
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
            oui.has_reconnect = g_settings.reconnect.valid;
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
                    if (g_settings.reconnect.valid) {
                        snprintf(oui.error_text, sizeof(oui.error_text),
                                 "Game no longer available");
                        settings_clear_reconnect();
                        g_settings.reconnect.valid = false;
                        oui.has_reconnect = false;
                    } else {
                        snprintf(oui.error_text, sizeof(oui.error_text),
                                 "Failed to connect to game server");
                    }
                    oui.subphase = ONLINE_SUB_ERROR;
                }
            }

            /* CREATE_WAITING: stay in waiting room, consume room status,
             * exit only when game starts (first state update from server) */
            if (oui.subphase == ONLINE_SUB_CREATE_WAITING) {
                ClientNetState cns = client_net_state();
                if (oui.room_assigned && cns == CLIENT_NET_ERROR) {
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
                        /* Persist reconnect info (CREATE path skips CONNECTING) */
                        char ip[NET_ADDR_LEN]; uint16_t port;
                        char rc[NET_ROOM_CODE_LEN]; uint8_t tok[NET_AUTH_TOKEN_LEN];
                        client_net_get_reconnect_info(ip, &port, rc, tok);
                        settings_save_reconnect(ip, port, rc, tok);
                        settings_load(&g_settings);
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
                } else if (cns == CLIENT_NET_CONNECTED) {
                    /* Consume room status updates for player list */
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
                    if (client_net_has_new_state()) {
                        /* First state update arrived — game is starting.
                         * Exit online UI; state_recv apply block sets gs.phase. */
                        /* Persist reconnect info now that the game has started */
                        {
                            char ip[NET_ADDR_LEN]; uint16_t port;
                            char rc[NET_ROOM_CODE_LEN]; uint8_t tok[NET_AUTH_TOKEN_LEN];
                            client_net_get_reconnect_info(ip, &port, rc, tok);
                            settings_save_reconnect(ip, port, rc, tok);
                            settings_load(&g_settings);
                        }
                        oui.subphase = ONLINE_SUB_MENU;
                    }
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

        /* Route commands to server when connected */
        if (client_net_state() == CLIENT_NET_CONNECTED) {
            InputCmd cmd;
            InputCmd local_cmds[INPUT_CMD_QUEUE_CAPACITY];
            int local_count = 0;

            /* Drain queue, split into server-bound and local.
             * Dealer DIR/AMT are local-only (visual feedback);
             * on CONFIRM, send DIR+AMT+CONFIRM to server in order. */
            while ((cmd = input_cmd_pop()).type != INPUT_CMD_NONE) {
                /* CONFIRM during SCORING is client-only (UI navigation),
                 * except for the last screen which syncs with server. */
                bool scoring_confirm = (cmd.type == INPUT_CMD_CONFIRM &&
                                        gs.phase == PHASE_SCORING);
                bool scoring_last_confirm = false;
                if (scoring_confirm) {
                    bool is_done_final = (rs.score_subphase == SCORE_SUB_DONE &&
                                          (!p2.enabled || game_state_is_game_over(&gs)));
                    bool is_contracts_final = (rs.score_subphase == SCORE_SUB_CONTRACTS &&
                                               rs.contract_reveal_count >= rs.contract_result_count);
                    scoring_last_confirm = is_done_final || is_contracts_final;
                }
                /* Dealer DIR/AMT: local visual feedback only.
                 * Apply inline — don't re-push to avoid queue-clear loss. */
                if (cmd.type == INPUT_CMD_DEALER_DIR) {
                    pps.dealer_dir = cmd.dealer_dir.direction;
                    rs.dealer_selected_dir = pps.dealer_dir;
                }
                else if (cmd.type == INPUT_CMD_DEALER_AMT) {
                    pps.dealer_amt = cmd.dealer_amt.amount;
                    rs.dealer_selected_amt = -1;
                    for (int i = 0; i < DEALER_AMOUNT_COUNT; i++) {
                        if (DEALER_AMOUNTS[i] == pps.dealer_amt) {
                            rs.dealer_selected_amt = i;
                            break;
                        }
                    }
                }
                /* Dealer CONFIRM: batch-send DIR+AMT+CONFIRM to server.
                 * Don't re-push — server drives the transition. */
                else if (cmd.type == INPUT_CMD_DEALER_CONFIRM) {
                    int seat = client_net_seat();
                    InputCmd dir_cmd = {
                        .type = INPUT_CMD_DEALER_DIR,
                        .source_player = seat,
                        .dealer_dir = { .direction = pps.dealer_dir },
                    };
                    InputCmd amt_cmd = {
                        .type = INPUT_CMD_DEALER_AMT,
                        .source_player = seat,
                        .dealer_amt = { .amount = pps.dealer_amt },
                    };
                    cmd.source_player = seat;
                    client_net_send_cmd(&dir_cmd);
                    client_net_send_cmd(&amt_cmd);
                    client_net_send_cmd(&cmd);
                }
                /* Contract SELECT: send to server, apply local effect
                 * inline (don't re-push to avoid re-routing). */
                else if (cmd.type == INPUT_CMD_SELECT_CONTRACT) {
                    cmd.source_player = client_net_seat();
                    client_net_send_cmd(&cmd);
                    if (pps.subphase == PASS_SUB_CONTRACT &&
                        p2.enabled && p2.round.draft.active) {
                        pps.draft_pick_pending = true;
                        pps.draft_pick_round = p2.round.draft.current_round;
                        rs.selected_contract_idx = cmd.contract.pair_index;
                    }
                }
                /* Transmute SELECT: only during card-pass subphase.
                 * Send to server, apply local effect inline. */
                else if (cmd.type == INPUT_CMD_SELECT_TRANSMUTATION &&
                         gs.phase == PHASE_PASSING &&
                         pps.subphase == PASS_SUB_CARD_PASS) {
                    cmd.source_player = client_net_seat();
                    client_net_send_cmd(&cmd);
                    if (p2.enabled) {
                        int tid = cmd.transmute_select.inv_slot;
                        pls.pending_transmutation = tid; /* -1 = deselect, else transmutation ID */
                    }
                }
                /* Transmute APPLY: only during card-pass subphase.
                 * Send to server, apply optimistically inline. */
                else if (cmd.type == INPUT_CMD_APPLY_TRANSMUTATION &&
                         gs.phase == PHASE_PASSING &&
                         pps.subphase == PASS_SUB_CARD_PASS) {
                    cmd.source_player = client_net_seat();
                    client_net_send_cmd(&cmd);
                    if (p2.enabled) {
                        int tid = pls.pending_transmutation;
                        int hand_idx = cmd.transmute_apply.hand_index;
                        Card old_card = (tid >= 0 && hand_idx >= 0 &&
                                         hand_idx < gs.players[0].hand.count)
                                      ? gs.players[0].hand.cards[hand_idx]
                                      : CARD_NONE;
                        if (tid >= 0 && hand_idx >= 0 &&
                            transmute_apply(&gs.players[0].hand,
                                            &p2.players[0].hand_transmutes,
                                            &p2.players[0].transmute_inv,
                                            hand_idx, tid, 0)) {
                            /* Update visual card identity so sync preserves selection */
                            Card new_card = gs.players[0].hand.cards[hand_idx];
                            int vi = (hand_idx < rs.hand_visual_counts[0])
                                   ? rs.hand_visuals[0][hand_idx] : -1;
                            if (vi >= 0 && vi < rs.card_count &&
                                card_equals(rs.cards[vi].card, old_card)) {
                                rs.cards[vi].card = new_card;
                                rs.cards[vi].transmute_id = tid;
                            }
                            pls.pending_transmutation = -1;
                            rs.sync_needed = true;
                        }
                    }
                }
                else if (scoring_last_confirm && !rs.scoring_ready_sent) {
                    /* Last scoring screen: send CONFIRM to server for sync,
                     * AND re-push locally so game_update sets scoring_screen_done */
                    cmd.source_player = client_net_seat();
                    client_net_send_cmd(&cmd);
                    rs.scoring_ready_sent = true;
                    if (local_count < INPUT_CMD_QUEUE_CAPACITY)
                        local_cmds[local_count++] = cmd;
                }
                else if (net_input_cmd_is_relevant((uint8_t)cmd.type) &&
                    !scoring_confirm) {
                    cmd.source_player = client_net_seat();
                    /* Save local seats before remapping for server */
                    int rogue_local_tp = cmd.rogue_reveal.target_player;
                    int duel_local_tp = cmd.duel_pick.target_player;
                    /* Remap local seats to server seats */
                    if (cmd.type == INPUT_CMD_ROGUE_REVEAL) {
                        cmd.rogue_reveal.target_player =
                            (rogue_local_tp + client_net_seat()) % NUM_PLAYERS;
                    } else if (cmd.type == INPUT_CMD_DUEL_PICK) {
                        cmd.duel_pick.target_player =
                            (duel_local_tp + client_net_seat()) % NUM_PLAYERS;
                    }
                    client_net_send_cmd(&cmd);
                    /* Instant human toss: fire animation locally before
                     * server round-trip for responsive feedback */
                    if (cmd.type == INPUT_CMD_CONFIRM &&
                        gs.phase == PHASE_PASSING &&
                        pps.subphase == PASS_SUB_CARD_PASS &&
                        gs.pass_card_count == 0) {
                        /* No cards to pass — just show waiting state */
                        rs.pass_ready_waiting = true;
                    } else if (cmd.type == INPUT_CMD_CONFIRM &&
                        gs.phase == PHASE_PASSING &&
                        pps.subphase == PASS_SUB_CARD_PASS &&
                        gs.pass_card_count > 0 &&
                        !pps.toss_started[0]) {
                        /* Populate pass_selections BEFORE toss so the
                         * animation can find card positions and hide them */
                        Card pass_cards[MAX_PASS_CARD_COUNT];
                        bool valid = true;
                        for (int i = 0; i < gs.pass_card_count; i++) {
                            int idx = rs.selected_indices[i];
                            if (idx < 0 || idx >= rs.card_count) {
                                valid = false;
                                break;
                            }
                            pass_cards[i] = rs.cards[idx].card;
                        }
                        if (valid &&
                            rs.selected_count == gs.pass_card_count) {
                            /* Set transmutation hints before select_pass */
                            for (int i = 0; i < gs.pass_card_count; i++) {
                                int vi = rs.selected_indices[i];
                                gs.pass_selection_hints[0][i] = -1;
                                if (p2.enabled) {
                                    int hand_idx = -1;
                                    for (int ci = 0; ci < rs.hand_visual_counts[0]; ci++) {
                                        if (rs.hand_visuals[0][ci] == vi) {
                                            hand_idx = ci;
                                            break;
                                        }
                                    }
                                    if (hand_idx >= 0) {
                                        const HandTransmuteState *hts = &p2.players[0].hand_transmutes;
                                        if (transmute_is_transmuted(hts, hand_idx))
                                            gs.pass_selection_hints[0][i] = hts->slots[hand_idx].transmutation_id;
                                    }
                                }
                            }
                            game_state_select_pass(&gs, 0, pass_cards,
                                                   gs.pass_card_count);
                            render_clear_selection(&rs);
                            pass_start_single_toss(&pps, &gs, &rs, 0);
                        }
                    } else if (cmd.type == INPUT_CMD_ROGUE_REVEAL &&
                               flow.step == FLOW_ROGUE_SUIT_CHOOSING) {
                        /* Suit chosen — server will respond with revealed cards */
                        flow.step = FLOW_ROGUE_WAITING;
                        rs.suit_hover_active = false;
                    } else if (cmd.type == INPUT_CMD_DUEL_PICK &&
                               flow.step == FLOW_DUEL_PICK_OPPONENT) {
                        int tp = duel_local_tp;
                        if (tp > 0 && tp < NUM_PLAYERS &&
                            tp != flow.duel_winner) {
                            flow.duel_target_player = tp;
                            flow.step = FLOW_DUEL_WAITING;
                            rs.opponent_hover_active = false;
                        }
                    } else if (cmd.type == INPUT_CMD_DUEL_GIVE &&
                               flow.step == FLOW_DUEL_PICK_OWN) {
                        int hi = cmd.duel_give.hand_index;
                        if (hi >= 0 && hi < gs.players[0].hand.count) {
                            flow.duel_own_card_idx = hi;
                        }
                    } else if (cmd.type == INPUT_CMD_DUEL_RETURN &&
                               flow.step == FLOW_DUEL_PICK_OWN) {
                        flow.duel_returned = true;
                    }
                } else if (cmd.type == INPUT_CMD_ROGUE_PICK &&
                           flow.step == FLOW_ROGUE_CHOOSING) {
                    /* Local-only: store target, transition to suit choosing */
                    int tp = cmd.rogue_pick.target_player;
                    if (tp > 0 && tp < NUM_PLAYERS && tp != flow.rogue_winner) {
                        flow.rogue_target_player = tp;
                        flow.rogue_reveal_player = tp;
                        flow.step = FLOW_ROGUE_SUIT_CHOOSING;
                        flow.timer = FLOW_ROGUE_SUIT_CHOOSE_TIME;
                        rs.opponent_hover_active = false;
                        rs.suit_hover_active = true;
                        rs.suit_hover_idx = -1;
                        rs.suit_border_t = 0.0f;
                        rs.rogue_target_player = tp;
                        render_chat_log_push(&rs, "Rogue: Choose a suit to reveal!");
                    }
                } else {
                    if (local_count < INPUT_CMD_QUEUE_CAPACITY)
                        local_cmds[local_count++] = cmd;
                }
            }

            /* Re-push local-only commands for game_update */
            for (int i = 0; i < local_count; i++) {
                input_cmd_push(local_cmds[i]);
            }
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
                if (cmd.type == INPUT_CMD_ONLINE_RECONNECT) {
                    if (g_settings.reconnect.valid) {
                        if (client_net_state() != CLIENT_NET_DISCONNECTED)
                            client_net_disconnect();
                        client_net_set_auth_token(
                            g_settings.reconnect.session_token);
                        {
                            const LobbyClientInfo *lci = lobby_client_info();
                            if (lci) client_net_set_username(lci->username);
                        }
                        client_net_connect(g_settings.reconnect.server_ip,
                                           g_settings.reconnect.server_port,
                                           g_settings.reconnect.room_code);
                        oui.subphase = ONLINE_SUB_CONNECTING;
                        oui.error_text[0] = '\0';
                    }
                } else if (cmd.type == INPUT_CMD_ONLINE_CREATE) {
                    /* Reset stale game-server connection (e.g. from a
                     * previous failed join) so CREATE_WAITING doesn't
                     * immediately see CLIENT_NET_ERROR */
                    if (client_net_state() != CLIENT_NET_DISCONNECTED)
                        client_net_disconnect();
                    lobby_client_create_room();
                    oui.subphase = ONLINE_SUB_CREATE_WAITING;
                    oui.player_count = 1;
                    oui.error_text[0] = '\0';
                } else if (cmd.type == INPUT_CMD_ONLINE_JOIN) {
                    if (oui.subphase == ONLINE_SUB_MENU ||
                        oui.subphase == ONLINE_SUB_ERROR) {
                        oui.subphase = ONLINE_SUB_JOIN_INPUT;
                        oui.room_code_len = 0;
                        oui.room_code_buf[0] = '\0';
                        oui.error_text[0] = '\0';
                        lobby_client_clear_error();
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
                } else if (cmd.type == INPUT_CMD_ONLINE_REMOVE_AI) {
                    if (oui.subphase == ONLINE_SUB_CREATE_WAITING &&
                        client_net_state() == CLIENT_NET_CONNECTED) {
                        client_net_send_remove_ai();
                    }
                } else if (cmd.type == INPUT_CMD_ONLINE_START) {
                    if (oui.subphase == ONLINE_SUB_CREATE_WAITING &&
                        client_net_state() == CLIENT_NET_CONNECTED) {
                        client_net_send_start_game(oui.ai_difficulty,
                            oui.timer_option, oui.point_goal, oui.gamemode);
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
                    lobby_client_clear_error();
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

        /* Capture phase before server state apply so SFX triggers
         * can detect transitions (e.g. entering PHASE_DEALING). */
        GamePhase phase_before_update = gs.phase;

        /* Online async pass: consume per-player confirmations every frame
         * (independent of state update availability). */
        if (gs.phase == PHASE_PASSING &&
            (pps.subphase == PASS_SUB_CARD_PASS ||
             pps.subphase == PASS_SUB_TOSS_ANIM) &&
            gs.pass_card_count > 0) {
            int confirmed_seat;
            while ((confirmed_seat = client_net_consume_pass_confirmed()) >= 0) {
                if (confirmed_seat >= 0 && confirmed_seat < NUM_PLAYERS)
                    gs.pass_ready[confirmed_seat] = true;
                if (!pps.toss_started[confirmed_seat])
                    pass_start_single_toss(&pps, &gs, &rs,
                                                  confirmed_seat);
            }
        }

        /* Step 9: Apply server state to local GameState + Phase2State.
         * Don't consume during trick animations — consuming with defer
         * permanently loses trick data since the state is popped from
         * the ring buffer. Leave it queued until flow is ready. */
        if (client_net_state() == CLIENT_NET_CONNECTED &&
            client_net_has_new_state()) {
            bool would_defer = (flow.step != FLOW_IDLE &&
                                flow.step != FLOW_WAITING_FOR_HUMAN &&
                                flow.step != FLOW_ROGUE_WAITING &&
                                flow.step != FLOW_DUEL_WAITING &&
                                !(flow.duel_watching &&
                                  (flow.step == FLOW_DUEL_PICK_OWN ||
                                   flow.step == FLOW_DUEL_ANIM_TO_CENTER ||
                                   flow.step == FLOW_DUEL_ANIM_RETURN ||
                                   flow.step == FLOW_DUEL_ANIM_EXCHANGE ||
                                   flow.step == FLOW_DUEL_RECEIVE_REVEAL ||
                                   flow.step == FLOW_DUEL_RECEIVE)));
            if (would_defer) {
                /* Even when deferring, peek at trick_winner so Roulette
                 * (random winner) is available for the pile animation.
                 * The full state update stays queued for later. */
                const NetPlayerView *peek = client_net_peek_state();
                if (peek && (int8_t)peek->trick_winner >= 0) {
                    int tw = peek->trick_winner;
                    pls.server_trick_winner =
                        (tw >= 0) ? (int)((tw - peek->my_seat + NUM_PLAYERS)
                                          % NUM_PLAYERS) : -1;
                }
                goto skip_state_apply;
            }

            /* Defer non-SCORING states while the scoring screen is active.
             * Allow consuming additional SCORING states (to get evaluated
             * contract data from the server's second SCORING broadcast). */
            if (gs.phase == PHASE_SCORING && !rs.scoring_screen_done) {
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

            /* Defer while online pass animation is playing */
            if (pps.pass_anim && !pps.async_toss)
                goto skip_state_apply;

            /* Defer while online deal animation is playing */
            if (rs.deal_anim)
                goto skip_state_apply;

            /* Defer while in settings — don't overwrite phase or game state */
            if (gs.phase == PHASE_SETTINGS)
                goto skip_state_apply;

            /* Defer during async toss — we need the PLAYING state to arrive
             * to get received card identities, but don't consume it yet if
             * tosses are still in flight. Wait for all toss anims + PLAYING. */
            if (pps.async_toss && pps.pass_anim)
                goto skip_state_apply;

            /* Detect PASSING→PLAYING transition: diff hands to find
             * passed/received cards, then start or complete animations.
             *
             * Note: gs.pass_ready[0] and gs.pass_selections[0] are NOT set
             * locally because CONFIRM and SELECT_CARD are server-only commands
             * The server also clears pass_selections after
             * execute_pass, so they aren't in the PLAYING state either.
             * We must derive everything from the hand diff. */
            if (gs.phase == PHASE_PASSING &&
                (pps.subphase == PASS_SUB_CARD_PASS ||
                 pps.subphase == PASS_SUB_TOSS_ANIM) &&
                gs.pass_card_count > 0) {
                const NetPlayerView *peek = client_net_peek_state();
                if (peek && peek->phase == PHASE_PLAYING) {
                    const Hand *old_hand = &gs.players[0].hand;
                    Card passed[MAX_PASS_CARD_COUNT];
                    Card received[MAX_PASS_CARD_COUNT];
                    int pass_count = 0;
                    int recv_count = 0;

                    /* Build a matched set: for each old card, try to find it
                     * in the new hand. Unmatched old = passed, unmatched new = received. */
                    bool old_matched[MAX_HAND_SIZE] = {false};
                    bool new_matched[MAX_HAND_SIZE] = {false};

                    for (int o = 0; o < old_hand->count; o++) {
                        for (int n = 0; n < peek->hand_count; n++) {
                            if (new_matched[n]) continue;
                            Card nc = net_card_to_game(peek->hand[n]);
                            if (card_equals(old_hand->cards[o], nc)) {
                                old_matched[o] = true;
                                new_matched[n] = true;
                                break;
                            }
                        }
                    }

                    /* Unmatched old cards = passed away */
                    for (int o = 0; o < old_hand->count &&
                         pass_count < MAX_PASS_CARD_COUNT; o++) {
                        if (!old_matched[o])
                            passed[pass_count++] = old_hand->cards[o];
                    }

                    /* Unmatched new cards = received */
                    for (int n = 0; n < peek->hand_count &&
                         recv_count < MAX_PASS_CARD_COUNT; n++) {
                        if (!new_matched[n])
                            received[recv_count++] =
                                net_card_to_game(peek->hand[n]);
                    }

                    /* Populate pass_selections[0] so the toss animation
                     * can read them for the human card fly-away. */
                    for (int i = 0; i < pass_count; i++)
                        gs.pass_selections[0][i] = passed[i];

                    if (pps.async_toss) {
                        /* Async mode: tosses already in flight. Assign
                         * received card identities to staged cards and
                         * fire catch-up tosses for any late seats. */
                        pass_assign_received_cards(&pps, &rs,
                                                          received, recv_count);
                        /* Fire catch-up tosses for any seats not yet started */
                        for (int p = 0; p < NUM_PLAYERS; p++) {
                            if (!pps.toss_started[p]) {
                                pass_start_single_toss(&pps, &gs, &rs, p);
                            }
                        }
                        pps.pass_anim = true;
                    } else {
                        /* No async tosses happened — fall back to batched */
                        pass_start_toss_anim_batched(&pps, &gs, &rs,
                                                    received, recv_count);
                    }
                    goto skip_state_apply;
                }
            }

            NetPlayerView view;
            client_net_consume_state(&view);

            int prev_round = gs.round_number;
            GamePhase phase_before_apply = gs.phase;

            state_recv_apply(&gs, &p2, &view, false);

            /* Apply configured turn time limit from server */
            if (view.turn_time_limit > 0.0f && view.turn_time_limit <= 120.0f)
                flow.turn_time_limit = view.turn_time_limit;

            /* Apply trick transmutation info from server to PlayPhaseState
             * so info_sync can detect Mirror and other per-slot effects */
            for (int i = 0; i < CARDS_PER_TRICK; i++) {
                const NetTrickTransmuteView *ttv = &view.trick_transmutes;
                int my = view.my_seat;
                pls.current_tti.transmutation_ids[i] = ttv->transmutation_ids[i];
                pls.current_tti.resolved_effects[i] =
                    (TransmuteEffect)ttv->resolved_effects[i];
                pls.current_tti.fogged[i] = ttv->fogged[i];
                pls.current_tti.transmuter_player[i] =
                    (ttv->transmuter_player[i] < 0) ? ttv->transmuter_player[i]
                    : (ttv->transmuter_player[i] - my + NUM_PLAYERS) % NUM_PLAYERS;
                pls.current_tti.fog_transmuter[i] =
                    (ttv->fog_transmuter[i] < 0) ? ttv->fog_transmuter[i]
                    : (ttv->fog_transmuter[i] - my + NUM_PLAYERS) % NUM_PLAYERS;
            }

            /* Inject client-side deal animation on new round.
             * Hand data is already populated from the server state;
             * sync_deal() will create face-down card visuals.
             * Skip on reconnect: phase_before_apply will be PHASE_ONLINE_MENU
             * or PHASE_MENU, not an in-game phase. */
            if (gs.round_number != prev_round &&
                is_ingame_phase(phase_before_apply)) {
                gs.phase = PHASE_DEALING;
                rs.deal_anim = true;
                rs.sync_needed = true;
                /* Reset async toss tracking for the new round */
                client_net_reset_pass_confirmed();
                memset(pps.toss_started, 0, sizeof(pps.toss_started));
                pps.toss_count = 0;
                pps.async_toss = false;
                pps.pass_auto_sent = false;
            }

            {
                PassSubphase new_sub = (PassSubphase)view.pass_subphase;
                /* Don't let server state overwrite client-side animation
                 * subphase during async toss (server still says CARD_PASS
                 * while client is in TOSS_ANIM or later) */
                if (!(pps.async_toss &&
                      pps.subphase >= PASS_SUB_TOSS_ANIM)) {
                    if (new_sub != pps.subphase)
                        pps.timer = 0.0f;
                    pps.subphase = new_sub;
                }
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
                int ft = view.trick_transmutes.fog_transmuter[i];
                pls.current_tti.fog_transmuter[i] =
                    (ft >= 0) ? (int)((ft - view.my_seat + NUM_PLAYERS)
                                      % NUM_PLAYERS) : -1;
            }

            /* Server-authoritative trick winner (Roulette determinism) */
            {
                int tw = view.trick_winner;
                pls.server_trick_winner =
                    (tw >= 0) ? (int)((tw - view.my_seat + NUM_PLAYERS)
                                      % NUM_PLAYERS) : -1;
            }

            /* Clear draft pending flag once server confirms the pick.
             * With AI players the server may advance the round in the
             * same tick, so also clear when the round moves past the
             * one we picked in. */
            if (pps.draft_pick_pending) {
                if (view.my_draft.has_picked_this_round ||
                    view.draft_current_round != pps.draft_pick_round ||
                    !view.draft_active ||
                    pps.subphase != PASS_SUB_CONTRACT) {
                    pps.draft_pick_pending = false;
                }
            }

            rs.sync_needed = true;

            /* Sync pass phase UI from server state */
            pass_sync_ui(&pps, &gs, &rs, &p2);
        }
        skip_state_apply: ;

        /* Game-over ELO data from server */
        if (client_net_has_game_over()) {
            const NetMsgGameOver *go = client_net_get_game_over();
            rs.elo_has_data = go->has_elo;
            if (go->has_elo) {
                for (int i = 0; i < NUM_PLAYERS; i++) {
                    rs.elo_prev[i] = go->prev_elo[i];
                    rs.elo_new[i]  = go->new_elo[i];
                }
            }
        }

        /* Step 13: Display server error messages in chat log */
        {
            char errbuf[NET_MAX_CHAT_LEN];
            if (client_net_consume_error(errbuf, sizeof(errbuf))) {
                render_chat_log_push_color(&rs, errbuf, ORANGE);
            }
        }

        /* Display server chat/system messages */
        while (client_net_has_chat()) {
            char chat_buf[NET_MAX_CHAT_LEN];
            uint8_t clr[3];
            int16_t tid;
            char hl[32];
            client_net_consume_chat(chat_buf, sizeof(chat_buf),
                                    clr, &tid, hl);
            Color c = (clr[0] || clr[1] || clr[2])
                ? (Color){clr[0], clr[1], clr[2], 255} : LIGHTGRAY;
            if (hl[0] && tid >= 0) {
                render_chat_log_push_rich(&rs, chat_buf, c, hl, tid);
            } else if (hl[0] && strncmp(hl, "trick ", 6) == 0) {
                int trick_num = atoi(hl + 6);
                render_chat_log_push_trick(&rs, chat_buf, c, hl, trick_num);
            } else {
                render_chat_log_push_color(&rs, chat_buf, c);
            }
        }

        while (clk.accumulator >= FIXED_DT) {
            game_update(&gs, &rs, &p2, &pps, &pls, &lui, &oui, &sui,
                        &g_settings, &flow, FIXED_DT, &quit_requested);
            clk.accumulator -= FIXED_DT;
        }

        /* Note: no queue clear needed here. All server-sent commands
         * are handled inline in the routing block above (never re-pushed
         * to local_cmds). Remaining queue entries are local-only commands
         * (scoring CONFIRM, CLICK, settings, etc.) that are safe to
         * survive across frames until game_update ticks. */

        /* Reset flow if we just returned to menu mid-game (e.g. pause → return to menu) */
        if (is_ingame_phase(phase_before_update) && gs.phase == PHASE_MENU)
            flow_init(&flow);

        audio_update(&audio, clk.raw_dt, anim_get_speed());

        phase_transition_update(&gs, &rs, &p2, &pps, &pls, &flow,
                                &prev_phase, &prev_hearts_broken);

        /* Sync pass UI after deal animation completes.
         * pass_sync_ui is normally called during state
         * consumption, but gs.phase is PHASE_DEALING at that point
         * (deal animation override), so the call is a no-op.
         * Re-sync here to catch the DEALING→PASSING transition. */
        pass_sync_ui(&pps, &gs, &rs, &p2);

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
                            clk.raw_dt);

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
                   clk.raw_dt);

        /* Pass turn timer to render state for display */
        rs.turn_time_remaining = flow.turn_timer;

        /* Pass duel state to render */
        rs.duel_watching = flow.duel_watching;
        if (!flow.duel_watching &&
            (flow.step == FLOW_DUEL_PICK_OPPONENT ||
             flow.step == FLOW_DUEL_PICK_OWN))
            rs.duel_time_remaining = flow.timer;
        else
            rs.duel_time_remaining = -1.0f;

        /* Music: single background track for all phases */
        audio_set_music(&audio, MUSIC_BACKGROUND);

        /* Apply audio settings each frame (cheap, keeps volumes in sync) */
        audio_apply_settings(&audio, &g_settings);

        /* Stats screen: set availability flag and poll for async responses */
        rs.stats_available =
            (lobby_client_state() == LOBBY_AUTHENTICATED);
        if (gs.phase == PHASE_STATS) {
            if (rs.stats_loading) {
                PlayerFullStats fs;
                if (lobby_client_has_stats(&fs)) {
                    rs.stats_data = fs;
                    rs.stats_loaded = true;
                    rs.stats_loading = false;
                }
            }
            if (rs.leaderboard_loading) {
                LeaderboardData ld;
                if (lobby_client_has_leaderboard(&ld)) {
                    rs.leaderboard_data = ld;
                    rs.leaderboard_loaded = true;
                    rs.leaderboard_loading = false;
                }
            }
        }

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

    lobby_client_shutdown();
    client_net_shutdown();
    audio_shutdown(&audio);
    card_render_transmute_shutdown();
    card_render_shutdown();
    if (rs.fog_shader_loaded) UnloadShader(rs.fog_shader);
    if (rs.font_loaded) {
        for (int i = 0; i < FONT_SIZE_COUNT; i++)
            UnloadFont(rs.fonts[i]);
    }
    CloseWindow();
    net_platform_cleanup();
    return 0;
}
