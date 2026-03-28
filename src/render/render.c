/* ============================================================
 * @deps-implements: render.h
 * @deps-requires: render.h, online_ui.h, particle.h, anim.h, layout.h,
 *                 card_render.h, phase2/phase2_defs.h, net/lobby_client.h,
 *                 core/game_state.h (PHASE_STATS, PHASE_ONLINE_MENU),
 *                 core/card.h, core/settings.h, raylib.h, rlgl.h, math.h
 * @deps-last-changed: 2026-03-27 — Removed PASS_SUB_TRANSMUTE draw case
 * ============================================================ */

#include "render.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "card_render.h"
#include "core/hand.h"
#include "core/debug_log.h"
#include "game/login_ui.h"
#include "game/online_ui.h"
#include "phase2/phase2_state.h"
#include "phase2/phase2_defs.h"
#include "rlgl.h"

/* ---- Internal constants ---- */

#define TRICK_WINNER_DISPLAY_TIME 1.0f

#define HUMAN_PLAYER 0

/* Forward declarations */
static void draw_contracts_panel(const RenderState *rs, float s,
                                  const LayoutConfig *cfg);
static const char *player_name(int player_id);

/* Map player_id to screen position: 0=bottom, 1=left, 2=top, 3=right */
static PlayerPosition player_screen_pos(int player_id)
{
    static const PlayerPosition positions[NUM_PLAYERS] = {
        POS_BOTTOM, POS_LEFT, POS_TOP, POS_RIGHT
    };
    return positions[player_id];
}

/* ---- Text word-wrap helper ---- */

/* Measure or draw word-wrapped text.  Returns total height consumed.
 * If draw==false, only measures (no DrawText calls). */
static float text_wrapped(const char *text, float x, float y,
                          int font_size, float max_w, Color color,
                          bool draw)
{
    float line_h = (float)font_size + 2.0f;
    float cur_y = y;
    const char *p = text;

    while (*p) {
        /* Find how many chars fit on this line, preferring word boundaries */
        int best = 0;        /* last fitting break point (chars) */
        int best_word = 0;   /* last fitting break at a space boundary */
        int len = (int)strlen(p);
        for (int i = 1; i <= len; i++) {
            char tmp[CHAT_MSG_LEN];
            int n = (i < (int)sizeof(tmp)) ? i : (int)sizeof(tmp) - 1;
            memcpy(tmp, p, n);
            tmp[n] = '\0';
            if (MeasureText(tmp, font_size) > (int)max_w) break;
            best = i;
            if (p[i] == ' ' || p[i] == '\0') best_word = i;
        }
        /* Prefer breaking at a word boundary; fall back to mid-word
         * only if a single word is wider than the line. */
        if (best_word > 0) best = best_word;
        if (best == 0) best = 1; /* force at least 1 char */
        /* Skip leading space on the next line */
        int skip = best;
        while (p[skip] == ' ') skip++;

        if (draw) {
            char tmp[CHAT_MSG_LEN];
            int n = (best < (int)sizeof(tmp)) ? best : (int)sizeof(tmp) - 1;
            memcpy(tmp, p, n);
            tmp[n] = '\0';
            DrawText(tmp, (int)x, (int)cur_y, font_size, color);
        }
        cur_y += line_h;
        p += skip;
    }
    return cur_y - y;
}

/* Measure height of word-wrapped text without drawing. */
static float measure_text_wrapped(const char *text, int font_size, float max_w)
{
    return text_wrapped(text, 0, 0, font_size, max_w, WHITE, false);
}

/* Draw word-wrapped text. Returns total height consumed. */
static float draw_text_wrapped(const char *text, float x, float y,
                               int font_size, float max_w, Color color)
{
    return text_wrapped(text, x, y, font_size, max_w, color, true);
}

/* ---- Card visual helpers ---- */

int render_alloc_card_visual(RenderState *rs)
{
    if (rs->card_count >= MAX_CARD_VISUALS) return -1;
    int idx = rs->card_count++;
    memset(&rs->cards[idx], 0, sizeof(CardVisual));
    rs->cards[idx].scale = 1.0f;
    rs->cards[idx].opacity = 1.0f;
    rs->cards[idx].transmute_id = -1;
    rs->cards[idx].hover_t = 0.0f;
    rs->cards[idx].pile_owner = -1;
    return idx;
}

/* Internal alias for backward compat within this file */
static int alloc_card_visual(RenderState *rs)
{
    return render_alloc_card_visual(rs);
}

/* ---- Sync: rebuild visuals from game state ---- */

static void sync_hands(const GameState *gs, RenderState *rs)
{
    const LayoutConfig *cfg = &rs->layout;
    float ls = cfg->scale;

    rs->card_count = 0;
    rs->trick_visual_count = 0;

    for (int p = 0; p < NUM_PLAYERS; p++) {
        const Hand *hand = &gs->players[p].hand;
        rs->hand_visual_counts[p] = hand->count;

        Vector2 positions[MAX_HAND_SIZE];
        float rotations[MAX_HAND_SIZE];
        int count = 0;
        PlayerPosition spos = player_screen_pos(p);
        layout_hand_positions(spos, hand->count, cfg,
                              positions, rotations, &count);

        /* rel_scale is the per-player relative scale (1.0 human, 0.7 AI).
         * cv->scale = rel_scale * layout_scale so card_render produces
         * correct pixel dimensions from CARD_WIDTH_REF * cv->scale. */
        float rel_scale = (p == HUMAN_PLAYER) ? 1.0f : 0.7f;
        float card_scale = rel_scale * ls;
        float cw_s = cfg->card_width * rel_scale;
        float ch_s = cfg->card_height * rel_scale;

        /* Compute rotation origin based on player position.
         * Bottom: pivot at bottom-center (cards fan upward).
         * Opponents: all use top-center pivot — East/West are derived
         * from North by rotating the whole layout ±90°, so the same
         * origin works for all three. */
        Vector2 origin;
        if (spos == POS_BOTTOM) {
            origin = (Vector2){cw_s * 0.5f, ch_s};
        } else {
            origin = (Vector2){cw_s * 0.5f, 0.0f};
        }

        for (int i = 0; i < hand->count; i++) {
            int idx = alloc_card_visual(rs);
            if (idx < 0) break;
            rs->hand_visuals[p][i] = idx;

            CardVisual *cv = &rs->cards[idx];
            cv->card = hand->cards[i];
            cv->position = positions[i];
            cv->target = positions[i];
            cv->start = positions[i];
            cv->rotation = rotations[i];
            cv->target_rotation = rotations[i];
            cv->start_rotation = rotations[i];
            cv->origin = origin;
            cv->face_up = (p == HUMAN_PLAYER);
            cv->scale = card_scale;
            cv->opacity = 1.0f;
            cv->z_order = i;
            cv->selected = false;
            cv->hovered = false;
            cv->animating = false;
            cv->transmute_id = (p == HUMAN_PLAYER)
                                   ? rs->hand_transmute_ids[i] : -1;
            cv->fog_mode = (p == HUMAN_PLAYER) ? rs->hand_fog_mode[i] : 0;
            cv->fog_reveal_t = (cv->fog_mode > 0) ? 1.0f : 0.0f;
        }
    }

    /* Sync trick cards (skip during scoring/game-over — the last trick
     * has already been collected but current_trick still holds stale data) */
    const Trick *trick = &gs->current_trick;
    if (gs->phase != PHASE_PLAYING) {
        trick = NULL;
    }
    int trick_show = trick ? trick->num_played : 0;
    if (rs->trick_visible_count > 0 && trick_show > rs->trick_visible_count)
        trick_show = rs->trick_visible_count;
    for (int i = 0; trick != NULL && i < trick_show; i++) {
        int idx = alloc_card_visual(rs);
        if (idx < 0) break;
        rs->trick_visuals[i] = idx;
        rs->trick_visual_count++;

        int pid = trick->player_ids[i];
        PlayerPosition spos = player_screen_pos(pid);
        Vector2 trick_pos = layout_trick_position(spos, cfg);

        CardVisual *cv = &rs->cards[idx];
        cv->card = trick->cards[i];
        cv->position = trick_pos;
        cv->target = trick_pos;
        cv->start = trick_pos;
        cv->face_up = true;
        cv->scale = 0.85f * ls;
        cv->opacity = 1.0f;
        cv->z_order = 100 + i;
        cv->animating = false;
        cv->transmute_id = rs->trick_transmute_ids[i];
        cv->fog_mode = rs->trick_fog_mode[i];
        cv->fog_reveal_t = (cv->fog_mode > 0) ? 1.0f : 0.0f;
    }
}

static bool deal_animations_done(const RenderState *rs)
{
    if (rs->card_count == 0) return false;
    for (int i = 0; i < rs->card_count; i++) {
        if (rs->cards[i].animating) return false;
    }
    return true;
}

static void sync_deal(const GameState *gs, RenderState *rs)
{
    /* Build visuals at final hand positions first */
    sync_hands(gs, rs);

    Vector2 center = layout_board_center(&rs->layout);

    /* Overwrite each card to start at board center and animate to final pos.
     * Cards keep their per-player scale.  Position is adjusted so each card
     * is visually centred on the board regardless of its origin pivot.
     * Human (bigger) cards get higher z_order so they draw on top. */
    for (int p = 0; p < NUM_PLAYERS; p++) {
        for (int slot = 0; slot < rs->hand_visual_counts[p]; slot++) {
            int d = slot * NUM_PLAYERS + p; /* deal order index */
            int idx = rs->hand_visuals[p][slot];
            CardVisual *cv = &rs->cards[idx];

            Vector2 final_pos = cv->position;
            float final_rot = cv->rotation;

            /* Centre card on board: top-left = position - origin,
             * visual centre = top-left + (w/2, h/2),
             * so position = center + origin - (w/2, h/2). */
            float cw = CARD_WIDTH_REF * cv->scale;
            float ch = CARD_HEIGHT_REF * cv->scale;
            cv->position = (Vector2){
                center.x + cv->origin.x - cw * 0.5f,
                center.y + cv->origin.y - ch * 0.5f,
            };
            cv->rotation = 0.0f;
            cv->face_up = false;

            int layer = (p == HUMAN_PLAYER) ? 100 : 0;
            cv->z_order = layer + d;

            anim_start(cv, final_pos, final_rot,
                            ANIM_DEAL_CARD_DURATION, EASE_OUT_QUAD);
            cv->anim_delay = (float)d * ANIM_DEAL_CARD_STAGGER * anim_get_speed();
        }
    }

    rs->deal_complete = false;
}

static void sync_buttons(const GameState *gs, RenderState *rs)
{
    const LayoutConfig *cfg = &rs->layout;
    float s = cfg->scale;
    Rectangle btn_rect = layout_confirm_button(cfg);
    Vector2 bc = layout_board_center(cfg);

    /* ---- Main menu items ---- */
    if (gs->phase == PHASE_MENU) {
        static const struct {
            const char *label;
            const char *subtitle;
            bool        disabled;
        } menu_defs[MENU_ITEM_COUNT] = {
            [MENU_PLAY_ONLINE]   = {"Play Online",   NULL,            false},
            [MENU_PLAY_OFFLINE]  = {"Play Offline",   NULL,            false},
            [MENU_DECK_BUILDING] = {"Deck Building",  "(Coming Soon)", true},
            [MENU_STATISTICS]    = {"My Stats",        NULL,            false},
            [MENU_SETTINGS]      = {"Settings",        NULL,            false},
            [MENU_EXIT]          = {"Exit",            NULL,            false},
        };

        /* Menu uses true screen center, not the shifted board center. */
        float screen_cx = cfg->screen_width * 0.5f;
        float screen_cy = cfg->screen_height * 0.5f;

        float btn_w = 280.0f * s;
        float btn_h = 50.0f * s;
        float btn_gap = 12.0f * s;
        float total_h = MENU_ITEM_COUNT * btn_h +
                         (MENU_ITEM_COUNT - 1) * btn_gap;
        float menu_top_y = screen_cy - total_h * 0.5f + 40.0f * s;

        for (int i = 0; i < MENU_ITEM_COUNT; i++) {
            rs->menu_items[i].bounds = (Rectangle){
                screen_cx - btn_w * 0.5f,
                menu_top_y + (float)i * (btn_h + btn_gap),
                btn_w, btn_h
            };
            rs->menu_items[i].label    = menu_defs[i].label;
            rs->menu_items[i].subtitle = menu_defs[i].subtitle;
            rs->menu_items[i].disabled = menu_defs[i].disabled;
            rs->menu_items[i].visible  = true;
            rs->menu_items[i].pressed  = false;
        }
    } else {
        for (int i = 0; i < MENU_ITEM_COUNT; i++) {
            rs->menu_items[i].visible = false;
            rs->menu_items[i].hovered = false;
        }
    }

    /* ---- Login UI buttons ---- */
    if (gs->phase == PHASE_LOGIN) {
        float screen_cx = cfg->screen_width * 0.5f;
        float screen_cy = cfg->screen_height * 0.5f;
        float btn_w = 200.0f * s;
        float btn_h = 45.0f * s;

        rs->btn_login_submit.bounds = (Rectangle){
            screen_cx - btn_w * 0.5f,
            screen_cy + 30.0f * s,
            btn_w, btn_h
        };
        rs->btn_login_submit.label = "Register";
        rs->btn_login_submit.visible =
            rs->login_ui && rs->login_ui->show_username_input &&
            !rs->login_ui->awaiting_response;
        rs->btn_login_submit.disabled = false;

        rs->btn_login_retry.bounds = (Rectangle){
            screen_cx - btn_w * 0.5f,
            screen_cy + 20.0f * s,
            btn_w, btn_h
        };
        rs->btn_login_retry.label = "Retry";
        rs->btn_login_retry.visible =
            rs->login_ui && rs->login_ui->error_text[0];
        rs->btn_login_retry.disabled = false;
    } else {
        rs->btn_login_submit.visible = false;
        rs->btn_login_retry.visible = false;
    }

    /* ---- Online menu buttons ---- */
    if (gs->phase == PHASE_ONLINE_MENU && rs->online_ui) {
        float screen_cx = cfg->screen_width * 0.5f;
        float screen_cy = cfg->screen_height * 0.5f;
        float btn_w = 280.0f * s;
        float btn_h = 50.0f * s;
        float btn_gap = 12.0f * s;

        static const char *online_labels[ONLINE_BTN_COUNT] = {
            "Create Room", "Join Room", "Quick Match", "Back"
        };

        OnlineSubphase sub = rs->online_ui->subphase;
        bool show_menu = (sub == ONLINE_SUB_MENU);

        float total_h = ONLINE_BTN_COUNT * btn_h +
                         (ONLINE_BTN_COUNT - 1) * btn_gap;
        float top_y = screen_cy - total_h * 0.5f + 20.0f * s;

        for (int i = 0; i < ONLINE_BTN_COUNT; i++) {
            rs->online_btns[i].bounds = (Rectangle){
                screen_cx - btn_w * 0.5f,
                top_y + (float)i * (btn_h + btn_gap),
                btn_w, btn_h
            };
            rs->online_btns[i].label = online_labels[i];
            rs->online_btns[i].visible = show_menu;
            rs->online_btns[i].disabled = false;
        }

        /* Join submit button */
        float small_btn_w = 200.0f * s;
        float small_btn_h = 45.0f * s;
        rs->btn_online_join_submit.bounds = (Rectangle){
            screen_cx - small_btn_w * 0.5f,
            screen_cy + 30.0f * s,
            small_btn_w, small_btn_h
        };
        rs->btn_online_join_submit.label = "Join";
        rs->btn_online_join_submit.visible = (sub == ONLINE_SUB_JOIN_INPUT);
        rs->btn_online_join_submit.disabled = false;

        /* Add AI button (visible for creator in CREATE_WAITING with empty slots) */
        int occupied = 0;
        if (rs->online_ui && sub == ONLINE_SUB_CREATE_WAITING) {
            for (int i = 0; i < NET_MAX_PLAYERS; i++)
                if (rs->online_ui->player_names[i][0] != '\0') occupied++;
        }

        rs->btn_online_add_ai.bounds = (Rectangle){
            screen_cx - small_btn_w * 0.5f,
            screen_cy + 125.0f * s,
            small_btn_w, small_btn_h
        };
        rs->btn_online_add_ai.label = "Add AI";
        rs->btn_online_add_ai.visible = (sub == ONLINE_SUB_CREATE_WAITING);
        rs->btn_online_add_ai.disabled = (occupied >= NET_MAX_PLAYERS);

        /* Start Game button (enabled only when all 4 seats filled) */
        rs->btn_online_start_game.bounds = (Rectangle){
            screen_cx - small_btn_w * 0.5f,
            screen_cy + 175.0f * s,
            small_btn_w, small_btn_h
        };
        rs->btn_online_start_game.label = "Start Game";
        rs->btn_online_start_game.visible = (sub == ONLINE_SUB_CREATE_WAITING);
        rs->btn_online_start_game.disabled = (occupied < NET_MAX_PLAYERS);

        /* Cancel button (used in multiple sub-states) */
        rs->btn_online_cancel.bounds = (Rectangle){
            screen_cx - small_btn_w * 0.5f,
            screen_cy + 225.0f * s,
            small_btn_w, small_btn_h
        };
        rs->btn_online_cancel.label = (sub == ONLINE_SUB_ERROR) ? "Back" : "Cancel";
        rs->btn_online_cancel.visible =
            (sub == ONLINE_SUB_CREATE_WAITING ||
             sub == ONLINE_SUB_JOIN_INPUT ||
             sub == ONLINE_SUB_JOIN_WAITING ||
             sub == ONLINE_SUB_QUEUE_SEARCHING ||
             sub == ONLINE_SUB_ERROR);
        rs->btn_online_cancel.disabled = false;
    } else {
        for (int i = 0; i < ONLINE_BTN_COUNT; i++)
            rs->online_btns[i].visible = false;
        rs->btn_online_join_submit.visible = false;
        rs->btn_online_add_ai.visible = false;
        rs->btn_online_start_game.visible = false;
        rs->btn_online_cancel.visible = false;
    }

    /* ---- Stats screen button ---- */
    if (gs->phase == PHASE_STATS) {
        float screen_cx = cfg->screen_width * 0.5f;
        float screen_cy = cfg->screen_height * 0.5f;
        float btn_w = 200.0f * s;
        float btn_h = 45.0f * s;
        rs->btn_stats_back.bounds = (Rectangle){
            screen_cx - btn_w * 0.5f,
            screen_cy + 130.0f * s,
            btn_w, btn_h
        };
        rs->btn_stats_back.label = "Back";
        rs->btn_stats_back.visible = true;
        rs->btn_stats_back.disabled = false;
    } else {
        rs->btn_stats_back.visible = false;
    }

    rs->btn_confirm_pass.bounds = btn_rect;
    rs->btn_confirm_pass.label = "Confirm Pass";
    if (rs->pass_anim_in_progress) {
        rs->btn_confirm_pass.visible = false;
    } else if (rs->contract_ui_active) {
        rs->btn_confirm_pass.visible =
            (gs->phase == PHASE_PASSING &&
             rs->selected_count == gs->pass_card_count &&
             rs->selected_contract_idx >= 0);
    } else {
        rs->btn_confirm_pass.visible =
            (gs->phase == PHASE_PASSING && rs->selected_count == gs->pass_card_count);
    }

    /* Dealer button layout */
    if (gs->phase == PHASE_PASSING && rs->dealer_ui_active) {
        float btn_w = 90.0f * s;
        float btn_h = 36.0f * s;
        float gap = 10.0f * s;
        float total_w = 3.0f * btn_w + 2.0f * gap;
        float dir_y = bc.y - 60.0f * s;
        for (int i = 0; i < DEALER_DIR_BTN_COUNT; i++) {
            float bx2 = bc.x - total_w * 0.5f + (float)i * (btn_w + gap);
            rs->dealer_dir_btns[i].bounds = (Rectangle){bx2, dir_y, btn_w, btn_h};
            rs->dealer_dir_btns[i].visible = true;
        }
        float amt_w = 60.0f * s;
        float amt_total = 4.0f * amt_w + 3.0f * gap;
        float amt_y = bc.y + 10.0f * s;
        for (int i = 0; i < DEALER_AMT_BTN_COUNT; i++) {
            float bx2 = bc.x - amt_total * 0.5f + (float)i * (amt_w + gap);
            rs->dealer_amt_btns[i].bounds = (Rectangle){bx2, amt_y, amt_w, btn_h};
            rs->dealer_amt_btns[i].visible = true;
        }
        float cfm_w = 120.0f * s;
        float cfm_y = bc.y + 80.0f * s;
        rs->dealer_confirm_btn.bounds = (Rectangle){bc.x - cfm_w * 0.5f, cfm_y, cfm_w, btn_h};
        rs->dealer_confirm_btn.visible = true;
    } else {
        for (int i = 0; i < DEALER_DIR_BTN_COUNT; i++)
            rs->dealer_dir_btns[i].visible = false;
        for (int i = 0; i < DEALER_AMT_BTN_COUNT; i++)
            rs->dealer_amt_btns[i].visible = false;
        rs->dealer_confirm_btn.visible = false;
    }

    if (gs->phase == PHASE_GAME_OVER) {
        float screen_cx = cfg->screen_width * 0.5f;
        float screen_cy = cfg->screen_height * 0.5f;
        rs->btn_continue.bounds = (Rectangle){
            screen_cx - 100.0f * s,
            screen_cy + 180.0f * s,
            200.0f * s, 50.0f * s
        };
        rs->btn_continue.label = "Return to Menu";
        rs->btn_continue.visible = true;
    } else if (gs->phase == PHASE_SCORING) {
        /* Bounds always set; visibility controlled by scoring subphase logic */
        rs->btn_continue.bounds = (Rectangle){
            bc.x - 100.0f * s,
            cfg->board_y + cfg->board_size - 100.0f * s,
            200.0f * s, 50.0f * s
        };
        /* Don't overwrite label/visible — scoring animation controls those */
    } else {
        rs->btn_continue.visible = false;
    }

    /* ---- Transmutation inventory buttons ---- */
    {
        Rectangle lp = layout_left_panel_lower(cfg);
        float btn_w = lp.width - 16.0f * s;
        float btn_h = 28.0f * s;
        float btn_gap = 4.0f * s;
        /* Position after Contract + Bonuses sections.
         * Worst case: ~100px headers + 8 bonus lines * 14px = ~210px at 720p. */
        float y_start = lp.y + 260.0f * s;

        for (int i = 0; i < rs->transmute_btn_count; i++) {
            rs->transmute_btns[i].bounds = (Rectangle){
                lp.x + 8.0f * s,
                y_start + (float)i * (btn_h + btn_gap),
                btn_w, btn_h
            };
            rs->transmute_btns[i].visible = true;
            rs->transmute_btns[i].disabled = false;
            rs->transmute_btns[i].subtitle = NULL;
            rs->transmute_btns[i].pressed =
                (rs->pending_transmutation_id >= 0 &&
                 rs->transmute_btn_ids[i] == rs->pending_transmutation_id);
        }
    }

    /* ---- Settings screen buttons ---- */
    if (gs->phase == PHASE_SETTINGS) {
        float row_w = 500.0f * s;
        float row_h = 40.0f * s;
        float row_gap = 8.0f * s;
        float arrow_sz = 30.0f * s;
        float scx = cfg->screen_width * 0.5f;
        float scy = cfg->screen_height * 0.5f;
        float cx = scx;

        /* Tab bar layout */
        static const char *tab_labels[] = {"Display", "Gameplay", "Audio"};
        float tab_w = 140.0f * s;
        float tab_h = 36.0f * s;
        float tab_gap = 8.0f * s;
        float tab_total_w = SETTINGS_TAB_COUNT * tab_w + (SETTINGS_TAB_COUNT - 1) * tab_gap;
        float tab_start_x = scx - tab_total_w * 0.5f;
        float tab_y = scy - 220.0f * s;

        for (int t = 0; t < SETTINGS_TAB_COUNT; t++) {
            rs->settings_tab_btns[t].bounds = (Rectangle){
                tab_start_x + (float)t * (tab_w + tab_gap), tab_y, tab_w, tab_h};
            rs->settings_tab_btns[t].label = tab_labels[t];
            rs->settings_tab_btns[t].visible = true;
            rs->settings_tab_btns[t].disabled = false;
            rs->settings_tab_btns[t].subtitle = NULL;
        }

        /* Row ranges per tab: Display=0..2, Gameplay=3..5, Audio=6..8 */
        static const int tab_row_start[] = {0, 3, 6};
        static const int tab_row_end[]   = {3, 6, 9}; /* exclusive */
        int rstart = tab_row_start[rs->settings_tab];
        int rend   = tab_row_end[rs->settings_tab];

        static const char *labels[] = {
            "Window Mode", "Resolution", "FPS Cap",
            "Anim Speed", "AI Speed", "Auto-Sort Received",
            "Master Volume", "Music Volume", "SFX Volume"
        };

        float content_y = tab_y + tab_h + 30.0f * s;

        for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
            rs->settings_labels[i] = labels[i];
            rs->settings_disabled[i] = (i >= SETTINGS_ACTIVE_COUNT);

            bool in_tab = (i >= rstart && i < rend);
            if (!in_tab) {
                rs->settings_rows_prev[i].visible = false;
                rs->settings_rows_next[i].visible = false;
                continue;
            }

            float y = content_y + (float)(i - rstart) * (row_h + row_gap);

            /* [<] button */
            rs->settings_rows_prev[i].bounds = (Rectangle){
                cx - row_w * 0.5f + 160.0f * s, y + (row_h - arrow_sz) * 0.5f,
                arrow_sz, arrow_sz};
            rs->settings_rows_prev[i].label = "<";
            rs->settings_rows_prev[i].visible = true;
            rs->settings_rows_prev[i].disabled = rs->settings_disabled[i];
            rs->settings_rows_prev[i].subtitle = NULL;

            /* [>] button */
            rs->settings_rows_next[i].bounds = (Rectangle){
                cx + row_w * 0.5f - arrow_sz, y + (row_h - arrow_sz) * 0.5f,
                arrow_sz, arrow_sz};
            rs->settings_rows_next[i].label = ">";
            rs->settings_rows_next[i].visible = true;
            rs->settings_rows_next[i].disabled = rs->settings_disabled[i];
            rs->settings_rows_next[i].subtitle = NULL;
        }

        /* Apply button: only on Display tab */
        int visible_rows = rend - rstart;
        float after_rows_y = content_y + (float)visible_rows * (row_h + row_gap);

        if (rs->settings_tab == SETTINGS_TAB_DISPLAY) {
            rs->btn_settings_apply.bounds = (Rectangle){
                cx + row_w * 0.5f - 100.0f * s, after_rows_y, 100.0f * s, 30.0f * s};
            rs->btn_settings_apply.label = "Apply";
            rs->btn_settings_apply.visible = true;
            rs->btn_settings_apply.disabled = false;
            rs->btn_settings_apply.subtitle = NULL;
        } else {
            rs->btn_settings_apply.visible = false;
        }

        /* Back button — always below content area */
        rs->btn_settings_back.bounds = (Rectangle){
            scx - 80.0f * s,
            after_rows_y + 40.0f * s,
            160.0f * s, 45.0f * s};
        rs->btn_settings_back.label = "Back";
        rs->btn_settings_back.visible = true;
        rs->btn_settings_back.disabled = false;
        rs->btn_settings_back.subtitle = NULL;
    } else {
        rs->btn_settings_back.visible = false;
        rs->btn_settings_apply.visible = false;
        for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
            rs->settings_rows_prev[i].visible = false;
            rs->settings_rows_next[i].visible = false;
        }
        for (int t = 0; t < SETTINGS_TAB_COUNT; t++) {
            rs->settings_tab_btns[t].visible = false;
        }
    }

    /* ---- Pause menu buttons ---- */
    if (rs->pause_state != PAUSE_INACTIVE) {
        float screen_cx = cfg->screen_width * 0.5f;
        float screen_cy = cfg->screen_height * 0.5f;
        float pbtn_w = 280.0f * s;
        float pbtn_h = 50.0f * s;
        float pbtn_gap = 12.0f * s;

        if (rs->pause_state == PAUSE_MENU) {
            static const char *pause_labels[PAUSE_BTN_COUNT] = {
                "Continue", "Settings", "Return to Menu", "Quit Game"
            };
            float total_h = PAUSE_BTN_COUNT * pbtn_h +
                            (PAUSE_BTN_COUNT - 1) * pbtn_gap;
            float top_y = screen_cy - total_h * 0.5f + 40.0f * s;

            for (int i = 0; i < PAUSE_BTN_COUNT; i++) {
                rs->pause_btns[i].bounds = (Rectangle){
                    screen_cx - pbtn_w * 0.5f,
                    top_y + (float)i * (pbtn_h + pbtn_gap),
                    pbtn_w, pbtn_h
                };
                rs->pause_btns[i].label = pause_labels[i];
                rs->pause_btns[i].visible = true;
                rs->pause_btns[i].disabled = false;
                rs->pause_btns[i].subtitle = NULL;
            }
            rs->pause_confirm_yes.visible = false;
            rs->pause_confirm_no.visible = false;
        } else {
            /* Confirmation dialog: Yes / No side by side */
            for (int i = 0; i < PAUSE_BTN_COUNT; i++)
                rs->pause_btns[i].visible = false;

            float conf_w = 120.0f * s;
            float conf_h = 50.0f * s;
            float conf_gap = 20.0f * s;
            float conf_y = screen_cy + 20.0f * s;

            rs->pause_confirm_yes.bounds = (Rectangle){
                screen_cx - conf_w - conf_gap * 0.5f, conf_y, conf_w, conf_h};
            rs->pause_confirm_yes.label = "Yes";
            rs->pause_confirm_yes.visible = true;
            rs->pause_confirm_yes.disabled = false;
            rs->pause_confirm_yes.subtitle = NULL;

            rs->pause_confirm_no.bounds = (Rectangle){
                screen_cx + conf_gap * 0.5f, conf_y, conf_w, conf_h};
            rs->pause_confirm_no.label = "No";
            rs->pause_confirm_no.visible = true;
            rs->pause_confirm_no.disabled = false;
            rs->pause_confirm_no.subtitle = NULL;
        }
    } else {
        for (int i = 0; i < PAUSE_BTN_COUNT; i++)
            rs->pause_btns[i].visible = false;
        rs->pause_confirm_yes.visible = false;
        rs->pause_confirm_no.visible = false;
    }
}

/* ---- Public API ---- */

void render_init(RenderState *rs)
{
    memset(rs, 0, sizeof(*rs));
    rs->hover_card_index = -1;
    rs->drag.card_visual_idx = -1;
    rs->drag.hand_slot_origin = -1;
    rs->drag.hand_slot_current = -1;
    rs->drag.is_play_drag = false;
    rs->drag.rearrange_count = 0;
    rs->last_trick_winner = -1;
    rs->current_phase = PHASE_MENU;
    rs->pass_card_limit = DEFAULT_PASS_CARD_COUNT;
    rs->layout_dirty = true;
    rs->sync_needed = true;
    rs->anim_play_player = -1;
    rs->anim_trick_slot = -1;

    /* Initialize mutable layout with defaults */
    layout_recalculate(&rs->layout, 1280, 720);

    for (int i = 0; i < MAX_PASS_CARD_COUNT; i++) {
        rs->selected_indices[i] = -1;
    }

    rs->contract_option_count = 0;
    rs->selected_contract_idx = -1;
    rs->contract_ui_active = false;
    rs->show_contract_results = false;
    rs->contract_result_count = 0;
    rs->contract_scroll_y = 0.0f;
    rs->draft_round_display = 0;
    rs->draft_picks_made = 0;
    for (int i = 0; i < 4; i++) rs->draft_transmute_ids[i] = -1;
    rs->info_contract_count = 0;

    rs->transmute_btn_count = 0;
    rs->pending_transmutation_id = -1;
    rs->transmute_info_count = 0;
    for (int i = 0; i < MAX_HAND_SIZE; i++)
        rs->hand_transmute_ids[i] = -1;
    for (int i = 0; i < CARDS_PER_TRICK; i++)
        rs->trick_transmute_ids[i] = -1;

    for (int i = 0; i < CHAT_LOG_MAX; i++)
        rs->chat_colors[i] = LIGHTGRAY;

    rs->deal_complete = false;

    rs->pile_card_count = 0;
    rs->pile_anim_in_progress = false;
    rs->trick_anim_in_progress = false;

    rs->pass_staged_count = 0;
    rs->pass_anim_in_progress = false;
    rs->pass_wait_timer = 0.0f;

    rs->pause_state = PAUSE_INACTIVE;
    rs->settings_return_phase = PHASE_MENU;
    rs->settings_return_paused = false;

    particle_init(&rs->particles);

    /* Fog shader */
    rs->fog_shader = LoadShader(NULL, "assets/shaders/fog.fs");
    if (rs->fog_shader.id > 0) {
        rs->fog_loc_time = GetShaderLocation(rs->fog_shader, "time");
        rs->fog_loc_opacity = GetShaderLocation(rs->fog_shader, "opacity");
        rs->fog_loc_aspect = GetShaderLocation(rs->fog_shader, "aspect");
        rs->fog_shader_loaded = true;
    } else {
        rs->fog_shader_loaded = false;
    }
    for (int i = 0; i < MAX_HAND_SIZE; i++) rs->hand_fog_mode[i] = 0;
    for (int i = 0; i < CARDS_PER_TRICK; i++) rs->trick_fog_mode[i] = 0;
}

void render_reset_to_menu(RenderState *rs)
{
    rs->pause_state = PAUSE_INACTIVE;
    render_clear_piles(rs);
    rs->pass_staged_count = 0;
    rs->pass_anim_in_progress = false;
    rs->pile_anim_in_progress = false;
    rs->trick_anim_in_progress = false;
    rs->deal_complete = false;
    rs->card_count = 0;
    for (int i = 0; i < NUM_PLAYERS; i++)
        rs->hand_visual_counts[i] = 0;
    rs->trick_visual_count = 0;
    rs->sync_needed = true;
    memset(rs->hand_fog_mode, 0, sizeof(rs->hand_fog_mode));
    memset(rs->trick_fog_mode, 0, sizeof(rs->trick_fog_mode));
}

void render_update(const GameState *gs, RenderState *rs, float dt)
{
    rs->pass_card_limit = gs->pass_card_count;

    /* Detect phase change */
    if (gs->phase != rs->current_phase) {
        rs->phase_just_changed = true;
        rs->phase_timer = 0.0f;
        rs->current_phase = gs->phase;
        rs->layout_dirty = true;

        /* Clear selection on phase change */
        render_clear_selection(rs);
    } else {
        rs->phase_just_changed = false;
    }

    rs->phase_timer += dt;

    /* Pause freeze: update only pause/confirm button hover, then return */
    if (rs->pause_state != PAUSE_INACTIVE) {
        Vector2 mouse = GetMousePosition();
        for (int i = 0; i < PAUSE_BTN_COUNT; i++) {
            rs->pause_btns[i].hovered =
                rs->pause_btns[i].visible &&
                CheckCollisionPointRec(mouse, rs->pause_btns[i].bounds);
        }
        rs->pause_confirm_yes.hovered =
            rs->pause_confirm_yes.visible &&
            CheckCollisionPointRec(mouse, rs->pause_confirm_yes.bounds);
        rs->pause_confirm_no.hovered =
            rs->pause_confirm_no.visible &&
            CheckCollisionPointRec(mouse, rs->pause_confirm_no.bounds);
        sync_buttons(gs, rs);
        return;
    }

    /* Sync visuals only when game state has mutated or phase changed.
     * Block sync during pass toss/wait/receive to preserve staged visuals.
     * Leave sync_needed set so it fires once pass_anim_in_progress clears. */
    /* Force-cancel drag on phase change */
    if (rs->phase_just_changed && rs->drag.active) {
        render_cancel_drag(rs);
    }

    if ((rs->phase_just_changed || rs->sync_needed) &&
        !rs->pass_anim_in_progress && !rs->pile_anim_in_progress &&
        !rs->trick_anim_in_progress &&
        !rs->drag.active) {
        DBG(DBG_SYNC, "sync FIRING: phase_changed=%d anim_player=%d trick_slot=%d",
            rs->phase_just_changed, rs->anim_play_player, rs->anim_trick_slot);
        /* Clear any pending snap-back — sync rebuilds visuals */
        if (rs->drag.snap_back) {
            rs->drag.snap_back = false;
            rs->drag.card_visual_idx = -1;
            rs->drag.has_release_pos = false;
        }
        /* Save selected card identities before resync */
        Card saved_selected[MAX_PASS_CARD_COUNT];
        int saved_count = 0;
        for (int i = 0; i < rs->selected_count; i++) {
            int idx = rs->selected_indices[i];
            if (idx >= 0 && idx < rs->card_count) {
                saved_selected[saved_count++] = rs->cards[idx].card;
            }
        }

        int anim_player = rs->anim_play_player;
        rs->anim_play_player = -1;

        /* Save trick card visual states before resync */
        CardVisual saved_tricks[CARDS_PER_TRICK];
        int        saved_trick_count = 0;
        for (int i = 0; i < rs->trick_visual_count; i++) {
            int idx = rs->trick_visuals[i];
            if (idx >= 0 && idx < rs->card_count) {
                saved_tricks[saved_trick_count++] = rs->cards[idx];
            }
        }

        /* Save hover_t for human hand cards */
        struct { Card card; float hover_t; } saved_hover[MAX_HAND_SIZE];
        int saved_hover_count = 0;
        for (int i = 0; i < rs->hand_visual_counts[HUMAN_PLAYER]; i++) {
            int idx = rs->hand_visuals[HUMAN_PLAYER][i];
            if (idx >= 0 && idx < rs->card_count) {
                saved_hover[saved_hover_count].card = rs->cards[idx].card;
                saved_hover[saved_hover_count].hover_t = rs->cards[idx].hover_t;
                saved_hover_count++;
            }
        }

        if (gs->phase == PHASE_DEALING) {
            sync_deal(gs, rs);
        } else {
            sync_hands(gs, rs);
        }

        /* Restore selections by matching card identity.
         * Online: use server-authoritative pass_selections[0].
         * Offline: use saved visual state (server hasn't seen selections yet). */
        rs->selected_count = 0;
        if (gs->phase == PHASE_PASSING) {
            Card source[MAX_PASS_CARD_COUNT];
            int source_count = 0;
            if (rs->online) {
                /* Use server-authoritative selections if available;
                 * otherwise preserve local visual selections (player
                 * hasn't confirmed yet, so server has no data). */
                for (int i = 0; i < gs->pass_card_count && i < MAX_PASS_CARD_COUNT; i++) {
                    if (!card_is_none(gs->pass_selections[0][i]))
                        source[source_count++] = gs->pass_selections[0][i];
                }
                if (source_count == 0) {
                    source_count = saved_count;
                    for (int i = 0; i < saved_count; i++)
                        source[i] = saved_selected[i];
                }
            } else {
                source_count = saved_count;
                for (int i = 0; i < saved_count; i++)
                    source[i] = saved_selected[i];
            }
            for (int si = 0; si < source_count; si++) {
                for (int i = 0; i < rs->hand_visual_counts[HUMAN_PLAYER]; i++) {
                    int idx = rs->hand_visuals[HUMAN_PLAYER][i];
                    if (idx >= 0 && idx < rs->card_count &&
                        card_equals(rs->cards[idx].card, source[si])) {
                        rs->selected_indices[rs->selected_count++] = idx;
                        rs->cards[idx].selected = true;
                        break;
                    }
                }
            }
        }

        /* Restore trick card states — order is stable across resync */
        for (int i = 0; i < rs->trick_visual_count && i < saved_trick_count; i++) {
            int idx = rs->trick_visuals[i];
            if (idx >= 0 && idx < rs->card_count &&
                card_equals(rs->cards[idx].card, saved_tricks[i].card)) {
                CardVisual saved = saved_tricks[i];
                saved.card = rs->cards[idx].card;
                saved.scale = rs->cards[idx].scale;
                /* Preserve freshly-synced fog and transmute state */
                saved.fog_mode = rs->cards[idx].fog_mode;
                saved.fog_reveal_t = rs->cards[idx].fog_reveal_t;
                saved.transmute_id = rs->cards[idx].transmute_id;
                rs->cards[idx] = saved;
            }
        }

        /* Restore hover_t for human hand cards */
        for (int i = 0; i < rs->hand_visual_counts[HUMAN_PLAYER]; i++) {
            int idx = rs->hand_visuals[HUMAN_PLAYER][i];
            for (int j = 0; j < saved_hover_count; j++) {
                if (card_equals(rs->cards[idx].card, saved_hover[j].card)) {
                    rs->cards[idx].hover_t = saved_hover[j].hover_t;
                    break;
                }
            }
        }

        /* Animate a trick card from hand/release position to trick slot.
         * anim_trick_slot selects the specific card (online: sequential),
         * falling back to the last trick visual (offline: one at a time). */
        int anim_slot = rs->anim_trick_slot;
        rs->anim_trick_slot = -1;
        if (anim_slot < 0 || anim_slot >= rs->trick_visual_count)
            anim_slot = rs->trick_visual_count - 1;
        if (anim_player >= 0 && rs->trick_visual_count > 0) {
            int trick_idx = rs->trick_visuals[anim_slot];
            CardVisual *cv = &rs->cards[trick_idx];

            Vector2 start_pos;
            float start_rot = 0.0f;
            Vector2 target = cv->position;

            if (anim_player == HUMAN_PLAYER && rs->drag.has_release_pos) {
                start_pos = rs->drag.release_pos;
                int mode = rs->drag.release_mode;
                rs->drag.has_release_pos = false;

                if (!anim_toss_enabled()) {
                    /* Animations disabled — card stays at target */
                } else if (mode == TOSS_FLICK) {
                    anim_setup_toss(cv, start_pos, 0.0f, target,
                                    &rs->drag.velocity,
                                    ANIM_TOSS_DURATION, 0.0f);
                } else {
                    /* TOSS_CLICK or TOSS_DROP: simple straight-line animation */
                    cv->position = start_pos;
                    cv->rotation = 0.0f;
                    anim_start(cv, target, 0.0f, ANIM_PLAY_CARD_DURATION,
                                    EASE_OUT_QUAD);
                }
            } else {
                /* AI players: bezier toss with randomized "personality" */
                PlayerPosition spos = player_screen_pos(anim_player);
                start_pos = layout_trick_position(spos, &rs->layout);
                if (rs->hand_visual_counts[anim_player] > 0) {
                    int mid = rs->hand_visual_counts[anim_player] / 2;
                    int mid_idx = rs->hand_visuals[anim_player][mid];
                    const CardVisual *mid_cv = &rs->cards[mid_idx];
                    start_pos = (Vector2){
                        mid_cv->position.x - mid_cv->origin.x,
                        mid_cv->position.y - mid_cv->origin.y
                    };
                    start_rot = mid_cv->rotation;
                }

                if (anim_toss_enabled()) {
                    anim_setup_toss(cv, start_pos, start_rot, target,
                                    NULL, ANIM_TOSS_DURATION, 0.0f);
                } else {
                    cv->position = start_pos;
                    cv->rotation = start_rot;
                    anim_start(cv, target, 0.0f, ANIM_PLAY_CARD_DURATION,
                                    EASE_OUT_QUAD);
                }
            }

            /* Block further syncs while this card animation plays.
             * Set here (after animation setup) rather than in flow_update,
             * so the sync block isn't gated before the animation is created. */
            rs->trick_anim_in_progress = true;
            DBG(DBG_ANIM, "trick_anim SET: player=%d slot=%d",
                anim_player, rs->anim_trick_slot);
        }

        DBG(DBG_SYNC, "sync_needed CLEARED");
        rs->sync_needed = false;
    }

    /* Update cached display values (skip during SCORING — animation controls them) */
    if (gs->phase != PHASE_SCORING) {
        for (int i = 0; i < NUM_PLAYERS; i++) {
            rs->displayed_round_points[i] = gs->players[i].round_points;
            rs->displayed_total_scores[i] = gs->players[i].total_score;
        }
    }
    rs->displayed_pass_dir = gs->pass_direction;

    /* Trick winner display timer */
    if (rs->trick_winner_timer > 0.0f) {
        rs->trick_winner_timer -= dt;
    }

    /* Update animations */
    for (int i = 0; i < rs->card_count; i++) {
        anim_update(&rs->cards[i], dt);
    }
    for (int i = 0; i < rs->pile_card_count; i++) {
        anim_update(&rs->pile_cards[i], dt);
    }

    /* Scoring phase animation update */
    if (gs->phase == PHASE_SCORING) {
        float anim_mult = anim_get_speed();

        switch (rs->score_subphase) {
        case SCORE_SUB_CARDS_FLY: {
            rs->score_anim_timer += dt;
            /* Slide menu down after delay */
            float menu_elapsed = rs->score_anim_timer -
                                 ANIM_SCORING_MENU_DELAY * anim_mult;
            if (menu_elapsed > 0.0f) {
                float menu_dur = ANIM_SCORING_MENU_DURATION * anim_mult;
                float t = menu_elapsed / menu_dur;
                if (t > 1.0f) t = 1.0f;
                /* Ease out cubic */
                float et = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
                rs->score_menu_slide_y =
                    -rs->layout.board_size * (1.0f - et);
                if (t >= 1.0f) rs->score_menu_arrived = true;
            }

            /* Check if all scoring pile cards are done animating */
            bool all_done = true;
            for (int i = 0; i < rs->pile_card_count; i++) {
                if (rs->pile_cards[i].opacity > 0.0f &&
                    (rs->pile_cards[i].animating ||
                     rs->pile_cards[i].anim_delay > 0.0f)) {
                    all_done = false;
                    break;
                }
            }
            if (all_done) rs->score_cards_landed = true;

            /* Transition when both cards landed and menu arrived */
            if (rs->score_cards_landed && rs->score_menu_arrived) {
                rs->score_subphase = SCORE_SUB_DISPLAY;
                rs->btn_continue.visible = true;
                rs->btn_continue.label = "Continue";
            }
            break;
        }

        case SCORE_SUB_DISPLAY:
        case SCORE_SUB_DONE:
            /* Idle, waiting for input (handled in update.c) */
            break;

        case SCORE_SUB_CONTRACTS:
            /* Staggered reveal of contract rows */
            if (rs->contract_reveal_count < rs->contract_result_count) {
                rs->contract_reveal_timer -= dt;
                if (rs->contract_reveal_timer <= 0.0f) {
                    rs->contract_reveal_count++;
                    if (rs->contract_reveal_count < rs->contract_result_count) {
                        rs->contract_reveal_timer =
                            ANIM_CONTRACT_REVEAL_STAGGER * anim_mult;
                    } else {
                        /* All revealed — show button */
                        rs->btn_continue.visible = true;
                    }
                }
            }
            break;

        case SCORE_SUB_COUNT_UP: {
            float rate = ANIM_SCORING_COUNTUP_RATE * anim_mult;
            rs->score_countup_timer += dt;
            if (rs->score_countup_timer >= rate) {
                rs->score_countup_timer -= rate;
                bool any_remaining = false;
                for (int i = 0; i < NUM_PLAYERS; i++) {
                    if (rs->score_countup_round[i] > 0) {
                        rs->score_countup_round[i]--;
                        rs->displayed_total_scores[i]++;
                        any_remaining = true;
                    }
                }
                if (any_remaining) {
                    rs->score_tick_pending = true;
                } else {
                    rs->score_subphase = SCORE_SUB_DONE;
                    rs->btn_continue.visible = true;
                    rs->btn_continue.label =
                        game_state_is_game_over(gs) ? "Game Over" : "Next Round";
                }
            }
            break;
        }

        }
    }

    /* Check deal completion */
    if (gs->phase == PHASE_DEALING && !rs->deal_complete) {
        if (deal_animations_done(rs)) {
            rs->deal_complete = true;
            /* Flip human cards face-up and reset z_order */
            for (int i = 0; i < rs->hand_visual_counts[HUMAN_PLAYER]; i++) {
                int idx = rs->hand_visuals[HUMAN_PLAYER][i];
                rs->cards[idx].face_up = true;
            }
            for (int p = 0; p < NUM_PLAYERS; p++) {
                for (int i = 0; i < rs->hand_visual_counts[p]; i++) {
                    int idx = rs->hand_visuals[p][i];
                    rs->cards[idx].z_order = i;
                }
            }
        }
    }

    /* Update hover state */
    for (int i = 0; i < rs->hand_visual_counts[HUMAN_PLAYER]; i++) {
        int idx = rs->hand_visuals[HUMAN_PLAYER][i];
        rs->cards[idx].hovered = false;
    }
    rs->hover_card_index = -1;
    if (gs->phase == PHASE_PASSING || gs->phase == PHASE_PLAYING) {
        Vector2 mouse = GetMousePosition();
        int hit = render_hit_test_card(rs, mouse);
        if (hit >= 0) {
            rs->hover_card_index = hit;
            rs->cards[hit].hovered = true;
        }
    }

    /* Opponent hover (for Rogue/Duel card picking) */
    for (int p = 1; p < NUM_PLAYERS; p++) {
        for (int i = 0; i < rs->hand_visual_counts[p]; i++) {
            int idx = rs->hand_visuals[p][i];
            if (idx < 0 || idx >= rs->card_count) continue;
            rs->cards[idx].hovered = false;
        }
    }
    if (rs->opponent_hover_active) {
        Vector2 mouse = GetMousePosition();
        int opp = -1;
        int hit = render_hit_test_opponent_card(rs, mouse, &opp);
        if (hit >= 0) {
            rs->cards[hit].hovered = true;
        }
    }

    /* Drag tracking: smooth card follow */
    if (rs->drag.active && rs->drag.card_visual_idx >= 0 &&
        rs->drag.card_visual_idx < rs->card_count) {
        Vector2 mouse = GetMousePosition();
        int didx = rs->drag.card_visual_idx;
        CardVisual *dcv = &rs->cards[didx];

        /* Target: mouse minus grab offset, converted to pivot coords */
        float target_x = mouse.x - rs->drag.grab_offset.x + dcv->origin.x;
        float target_y = mouse.y - rs->drag.grab_offset.y + dcv->origin.y;

        /* Exponential smoothing for slight lag */
        float blend = 1.0f - expf(-20.0f * dt);
        rs->drag.current_pos.x += (target_x - rs->drag.current_pos.x) * blend;
        rs->drag.current_pos.y += (target_y - rs->drag.current_pos.y) * blend;

        dcv->position = rs->drag.current_pos;
        dcv->rotation = 0.0f;        /* straighten card while dragging */
        dcv->hovered = false;
        dcv->hover_t = 0.5f;         /* subtle lifted feel (7.5% scale, half lift) */

        /* Velocity tracking from raw (unsmoothed) target for accurate flick detection */
        if (dt > 0.0f) {
            rs->drag.velocity.x = (target_x - rs->drag.prev_pos.x) / dt;
            rs->drag.velocity.y = (target_y - rs->drag.prev_pos.y) / dt;
        }
        rs->drag.prev_pos = (Vector2){ target_x, target_y };

        /* Slot detection: find which hand slot the dragged card is over */
        int hand_count = rs->hand_visual_counts[HUMAN_PLAYER];
        if (hand_count > 1) {
            Vector2 slot_positions[MAX_HAND_SIZE];
            float slot_rotations[MAX_HAND_SIZE];
            int slot_count = 0;
            layout_hand_positions(POS_BOTTOM, hand_count, &rs->layout,
                                  slot_positions, slot_rotations, &slot_count);

            /* Find nearest slot by X distance */
            float drag_x = rs->drag.current_pos.x;
            int best_slot = rs->drag.hand_slot_current;
            float best_dist = 1e9f;
            for (int i = 0; i < slot_count; i++) {
                float d = fabsf(drag_x - slot_positions[i].x);
                if (d < best_dist) {
                    best_dist = d;
                    best_slot = i;
                }
            }

            if (best_slot != rs->drag.hand_slot_current) {
                rs->drag.hand_slot_current = best_slot;
                /* Rebuild rearrange_map based on new target slot */
                /* Map: insert dragged card's logical slot into the sequence */
                rs->drag.rearrange_count = 0;
                int src = rs->drag.hand_slot_origin;
                /* Build list of non-dragged hand indices in original order */
                int others[MAX_HAND_SIZE];
                int other_count = 0;
                for (int i = 0; i < hand_count; i++) {
                    if (i == src) continue;
                    others[other_count++] = i;
                }
                /* Build map — gap is created implicitly by the visual
                 * slide loop skipping target_slot when positioning. */
                for (int i = 0; i < other_count; i++) {
                    rs->drag.rearrange_map[rs->drag.rearrange_count++] = others[i];
                }
            }
        }
    }

    /* Visual rearranging: slide non-dragged cards to rearranged positions */
    if (rs->drag.active && rs->drag.rearrange_count > 0) {
        int hand_count = rs->hand_visual_counts[HUMAN_PLAYER];
        Vector2 slot_positions[MAX_HAND_SIZE];
        float slot_rotations[MAX_HAND_SIZE];
        int slot_count = 0;
        layout_hand_positions(POS_BOTTOM, hand_count, &rs->layout,
                              slot_positions, slot_rotations, &slot_count);

        float blend = 1.0f - expf(-ANIM_REARRANGE_BLEND_RATE * dt);
        int target_slot = rs->drag.hand_slot_current;
        int map_i = 0;

        for (int display = 0; display < hand_count && map_i < rs->drag.rearrange_count; display++) {
            if (display == target_slot) continue; /* skip the dragged card's slot */
            int orig_idx = rs->drag.rearrange_map[map_i++];
            int cv_idx = rs->hand_visuals[HUMAN_PLAYER][orig_idx];
            CardVisual *cv = &rs->cards[cv_idx];

            Vector2 target_pos = slot_positions[display];
            float target_rot = slot_rotations[display];

            cv->position.x += (target_pos.x - cv->position.x) * blend;
            cv->position.y += (target_pos.y - cv->position.y) * blend;
            cv->rotation += (target_rot - cv->rotation) * blend;
        }
    }

    /* Snap-back: animate card to original hand position */
    if (rs->drag.snap_back && rs->drag.card_visual_idx >= 0 &&
        rs->drag.card_visual_idx < rs->card_count) {
        CardVisual *cv = &rs->cards[rs->drag.card_visual_idx];
        cv->z_order = rs->drag.original_z;
        if (anim_toss_enabled()) {
            anim_start(cv, rs->drag.original_pos, rs->drag.original_rot,
                            ANIM_SNAP_BACK_DURATION, EASE_OUT_QUAD);
        } else {
            cv->position = rs->drag.original_pos;
            cv->rotation = rs->drag.original_rot;
        }
        rs->drag.snap_back = false;
        rs->drag.card_visual_idx = -1;
    }

    /* Animate hover_t for human hand cards (frame-rate independent) */
    for (int i = 0; i < rs->hand_visual_counts[HUMAN_PLAYER]; i++) {
        int idx = rs->hand_visuals[HUMAN_PLAYER][i];
        /* Skip dragged card */
        if (rs->drag.active && idx == rs->drag.card_visual_idx) continue;
        CardVisual *cv = &rs->cards[idx];
        float hover_target = cv->hovered ? 1.0f : 0.0f;
        float diff = hover_target - cv->hover_t;
        if (fabsf(diff) > 0.001f) {
            float blend = 1.0f - expf(-HOVER_ANIM_SPEED * dt);
            cv->hover_t += diff * blend;
            if (fabsf(hover_target - cv->hover_t) < 0.001f)
                cv->hover_t = hover_target;
        } else {
            cv->hover_t = hover_target;
        }
    }

    /* Animate hover_t for opponent hand cards (when opponent_hover_active) */
    if (rs->opponent_hover_active) {
        for (int p = 1; p < NUM_PLAYERS; p++) {
            for (int i = 0; i < rs->hand_visual_counts[p]; i++) {
                int idx = rs->hand_visuals[p][i];
                CardVisual *cv = &rs->cards[idx];
                float hover_target = cv->hovered ? 1.0f : 0.0f;
                float diff = hover_target - cv->hover_t;
                if (fabsf(diff) > 0.001f) {
                    float blend = 1.0f - expf(-HOVER_ANIM_SPEED * dt);
                    cv->hover_t += diff * blend;
                    if (fabsf(hover_target - cv->hover_t) < 0.001f)
                        cv->hover_t = hover_target;
                } else {
                    cv->hover_t = hover_target;
                }
            }
        }
    }

    /* Update transmute button hover state */
    if (rs->transmute_btn_count > 0 &&
        (gs->phase == PHASE_PLAYING || gs->phase == PHASE_PASSING)) {
        Vector2 mouse = GetMousePosition();
        for (int i = 0; i < rs->transmute_btn_count; i++) {
            rs->transmute_btns[i].hovered =
                rs->transmute_btns[i].visible &&
                !rs->transmute_btns[i].disabled &&
                CheckCollisionPointRec(mouse, rs->transmute_btns[i].bounds);
        }
    }

    /* Update contract button hover state */
    if (rs->contract_ui_active && gs->phase == PHASE_PASSING) {
        Vector2 mouse = GetMousePosition();
        for (int i = 0; i < rs->contract_option_count; i++) {
            rs->contract_options[i].hovered =
                rs->contract_options[i].visible &&
                CheckCollisionPointRec(mouse, rs->contract_options[i].bounds);
        }
    }

    /* Update online menu button hover state */
    if (gs->phase == PHASE_ONLINE_MENU) {
        Vector2 mouse = GetMousePosition();
        for (int i = 0; i < ONLINE_BTN_COUNT; i++) {
            rs->online_btns[i].hovered =
                rs->online_btns[i].visible &&
                CheckCollisionPointRec(mouse, rs->online_btns[i].bounds);
        }
        rs->btn_online_join_submit.hovered =
            rs->btn_online_join_submit.visible &&
            CheckCollisionPointRec(mouse, rs->btn_online_join_submit.bounds);
        rs->btn_online_add_ai.hovered =
            rs->btn_online_add_ai.visible &&
            !rs->btn_online_add_ai.disabled &&
            CheckCollisionPointRec(mouse, rs->btn_online_add_ai.bounds);
        rs->btn_online_start_game.hovered =
            rs->btn_online_start_game.visible &&
            !rs->btn_online_start_game.disabled &&
            CheckCollisionPointRec(mouse, rs->btn_online_start_game.bounds);
        rs->btn_online_cancel.hovered =
            rs->btn_online_cancel.visible &&
            CheckCollisionPointRec(mouse, rs->btn_online_cancel.bounds);
    }

    /* Update stats button hover state */
    if (gs->phase == PHASE_STATS) {
        Vector2 mouse = GetMousePosition();
        rs->btn_stats_back.hovered =
            rs->btn_stats_back.visible &&
            CheckCollisionPointRec(mouse, rs->btn_stats_back.bounds);
    }

    /* Update login button hover state */
    if (gs->phase == PHASE_LOGIN) {
        Vector2 mouse = GetMousePosition();
        rs->btn_login_submit.hovered =
            rs->btn_login_submit.visible &&
            CheckCollisionPointRec(mouse, rs->btn_login_submit.bounds);
        rs->btn_login_retry.hovered =
            rs->btn_login_retry.visible &&
            CheckCollisionPointRec(mouse, rs->btn_login_retry.bounds);
    }

    /* Update menu item hover state */
    if (gs->phase == PHASE_MENU) {
        Vector2 mouse = GetMousePosition();
        for (int i = 0; i < MENU_ITEM_COUNT; i++) {
            rs->menu_items[i].hovered =
                rs->menu_items[i].visible &&
                !rs->menu_items[i].disabled &&
                CheckCollisionPointRec(mouse, rs->menu_items[i].bounds);
        }
    }

    /* Update settings button hover state */
    if (gs->phase == PHASE_SETTINGS) {
        Vector2 mouse = GetMousePosition();
        rs->btn_settings_back.hovered =
            rs->btn_settings_back.visible &&
            CheckCollisionPointRec(mouse, rs->btn_settings_back.bounds);
        rs->btn_settings_apply.hovered =
            rs->btn_settings_apply.visible &&
            !rs->btn_settings_apply.disabled &&
            CheckCollisionPointRec(mouse, rs->btn_settings_apply.bounds);
        for (int t = 0; t < SETTINGS_TAB_COUNT; t++) {
            rs->settings_tab_btns[t].hovered =
                rs->settings_tab_btns[t].visible &&
                CheckCollisionPointRec(mouse, rs->settings_tab_btns[t].bounds);
        }
        for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
            rs->settings_rows_prev[i].hovered =
                rs->settings_rows_prev[i].visible &&
                !rs->settings_rows_prev[i].disabled &&
                CheckCollisionPointRec(mouse, rs->settings_rows_prev[i].bounds);
            rs->settings_rows_next[i].hovered =
                rs->settings_rows_next[i].visible &&
                !rs->settings_rows_next[i].disabled &&
                CheckCollisionPointRec(mouse, rs->settings_rows_next[i].bounds);
        }
    }

    sync_buttons(gs, rs);

    /* Update particles */
    particle_update(&rs->particles, dt);
}

/* ---- Drawing helpers ---- */

static void draw_card_visual(const CardVisual *cv, float ui_scale,
                             const RenderState *rs)
{
    Vector2 pos = cv->position;

    /* Hover pop: scale up and lift */
    float hover_scale = 1.0f + (HOVER_SCALE_TARGET - 1.0f) * cv->hover_t;
    float effective_scale = cv->scale * hover_scale;
    float hover_lift = HOVER_LIFT_REF * ui_scale * cv->hover_t;
    pos.y -= hover_lift;

    /* Scale origin proportionally */
    Vector2 origin = {
        cv->origin.x * hover_scale,
        cv->origin.y * hover_scale,
    };

    /* Lift selected cards (upward for bottom player) */
    if (cv->selected) {
        pos.y -= CARD_SELECT_LIFT_REF * ui_scale;
    }

    bool visible = cv->face_up || (cv->revealed_to & 1); /* bit 0 = player 0 = viewer */
    if (visible) {
        if (cv->transmute_id >= 0 &&
            card_render_has_transmute_sprite(cv->transmute_id)) {
            card_render_transmute_face(cv->transmute_id, pos, effective_scale,
                                       cv->opacity, cv->hovered, cv->selected,
                                       cv->rotation, origin);
        } else {
            card_render_face(cv->card, pos, effective_scale, cv->opacity,
                             cv->hovered, cv->selected,
                             cv->rotation, origin);
        }
    } else {
        card_render_back(pos, effective_scale, cv->opacity,
                         cv->rotation, origin);
    }

    /* Dim overlay for unplayable cards.  Drawn per-card so that later
     * cards in the hand paint over earlier dim rects — no overlap darkening. */
    if (cv->dimmed) {
        float cw = CARD_WIDTH_REF * effective_scale;
        float ch = CARD_HEIGHT_REF * effective_scale;

        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, 0.0f);
        rlRotatef(cv->rotation, 0.0f, 0.0f, 1.0f);
        rlTranslatef(-origin.x, -origin.y, 0.0f);

        Rectangle dim_rect = {0, 0, cw, ch};
        DrawRectangleRounded(dim_rect, 0.15f, 4, (Color){0, 0, 0, 100});

        rlPopMatrix();
    }

    /* Shield negation overlay: dim card + small shield icon */
    if (cv->shielded && cv->face_up && cv->opacity > 0.0f) {
        float cw = CARD_WIDTH_REF * effective_scale;
        float ch = CARD_HEIGHT_REF * effective_scale;

        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, 0.0f);
        rlRotatef(cv->rotation, 0.0f, 0.0f, 1.0f);
        rlTranslatef(-origin.x, -origin.y, 0.0f);

        /* Dim overlay */
        Rectangle dim_rect = {0, 0, cw, ch};
        DrawRectangleRounded(dim_rect, 0.15f, 4, (Color){0, 0, 0, 120});

        /* Small shield icon centered on card */
        float sr = ch * 0.18f; /* shield size relative to card */
        float sx = cw * 0.5f;
        float sy = ch * 0.45f;
        float shw = sr * 0.7f;
        float s_top = sy - sr;
        float s_mid = sy + sr * 0.3f;
        float s_bot = sy + sr;
        Vector2 stl = {sx - shw, s_top};
        Vector2 str_ = {sx + shw, s_top};
        Vector2 sbr = {sx + shw, s_mid};
        Vector2 sbl = {sx - shw, s_mid};
        Vector2 stip = {sx, s_bot};
        Color sfill = (Color){220, 190, 50, 220};
        Color sedge = (Color){220, 190, 50, 255};
        float slw = 1.5f * ui_scale;
        DrawTriangle(stl, str_, sbl, sfill);
        DrawTriangle(sbl, str_, sbr, sfill);
        DrawTriangle(sbl, sbr, stip, sfill);
        DrawLineEx(stl, str_, slw, sedge);
        DrawLineEx(str_, sbr, slw, sedge);
        DrawLineEx(sbr, stip, slw, sedge);
        DrawLineEx(stip, sbl, slw, sedge);
        DrawLineEx(sbl, stl, slw, sedge);

        rlPopMatrix();
    }

    /* Inversion overlay: dim card + down arrow icon */
    if (cv->inverted && cv->face_up && cv->opacity > 0.0f) {
        float cw = CARD_WIDTH_REF * effective_scale;
        float ch = CARD_HEIGHT_REF * effective_scale;

        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, 0.0f);
        rlRotatef(cv->rotation, 0.0f, 0.0f, 1.0f);
        rlTranslatef(-origin.x, -origin.y, 0.0f);

        /* Dim overlay */
        Rectangle dim_rect = {0, 0, cw, ch};
        DrawRectangleRounded(dim_rect, 0.15f, 4, (Color){0, 0, 0, 120});

        /* Down arrow icon centered on card */
        float ar = ch * 0.18f; /* arrow size relative to card */
        float ax = cw * 0.5f;
        float ay = ch * 0.40f;
        Color afill = (Color){50, 200, 220, 220};
        Color aedge = (Color){50, 200, 220, 255};
        float alw = 1.5f * ui_scale;

        /* Stem rectangle */
        float stem_hw = ar * 0.25f;
        float stem_top = ay - ar * 0.6f;
        float stem_bot = ay + ar * 0.1f;
        DrawRectangleRec((Rectangle){ax - stem_hw, stem_top,
                                      stem_hw * 2.0f, stem_bot - stem_top},
                         afill);

        /* Triangle pointing down */
        float tri_hw = ar * 0.7f;
        float tri_top = ay;
        float tri_bot = ay + ar;
        Vector2 atl = {ax - tri_hw, tri_top};
        Vector2 atr = {ax + tri_hw, tri_top};
        Vector2 atip = {ax, tri_bot};
        DrawTriangle(atl, atr, atip, afill);
        DrawLineEx(atl, atr, alw, aedge);
        DrawLineEx(atr, atip, alw, aedge);
        DrawLineEx(atip, atl, alw, aedge);

        rlPopMatrix();
    }

    /* Transmuted card overlay: purple border + ID badge.
     * Use the same rlgl matrix transforms as card_render_face so the
     * overlay aligns with the card even when rotated. */
    if (cv->transmute_id >= 0 && visible && cv->opacity > 0.0f) {
        float cw = CARD_WIDTH_REF * effective_scale;
        float ch = CARD_HEIGHT_REF * effective_scale;

        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, 0.0f);
        rlRotatef(cv->rotation, 0.0f, 0.0f, 1.0f);
        rlTranslatef(-origin.x, -origin.y, 0.0f);

        /* (0,0) = card top-left in transformed space */
        Rectangle card_rect = {0, 0, cw, ch};
        DrawRectangleLinesEx(card_rect, 3.0f * ui_scale, PURPLE);

        /* ID badge in top-right corner — only for cards without a custom sprite */
        if (!card_render_has_transmute_sprite(cv->transmute_id)) {
            char id_buf[8];
            snprintf(id_buf, sizeof(id_buf), "%d", cv->transmute_id);
            int badge_fs = (int)(12.0f * ui_scale);
            int tw = MeasureText(id_buf, badge_fs);
            int bx = (int)(cw - (float)tw - 4.0f * ui_scale);
            int by = (int)(3.0f * ui_scale);
            DrawRectangle(bx - 2, by - 1, tw + 4, badge_fs + 2,
                          (Color){80, 0, 120, 200});
            DrawText(id_buf, bx, by, badge_fs, WHITE);
        }

        rlPopMatrix();
    }

    /* Fog overlay — fits inside card, does not cover purple border */
    if (cv->fog_mode > 0 && cv->fog_reveal_t > 0.01f && cv->opacity > 0.0f &&
        rs && rs->fog_shader_loaded) {
        float cw = CARD_WIDTH_REF * effective_scale;
        float ch = CARD_HEIGHT_REF * effective_scale;
        float fog_opacity = (cv->fog_mode == 1) ? 0.45f * cv->fog_reveal_t
                                                 : 1.0f * cv->fog_reveal_t;
        float t = (float)GetTime();
        float aspect = ch / cw; /* card height/width ratio (~1.5) */
        SetShaderValue(rs->fog_shader, rs->fog_loc_time, &t, SHADER_UNIFORM_FLOAT);
        SetShaderValue(rs->fog_shader, rs->fog_loc_opacity, &fog_opacity,
                       SHADER_UNIFORM_FLOAT);
        SetShaderValue(rs->fog_shader, rs->fog_loc_aspect, &aspect,
                       SHADER_UNIFORM_FLOAT);

        /* Inset to avoid covering the purple transmute border (3px at ui_scale) */
        float inset = 3.0f * ui_scale;

        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, 0.0f);
        rlRotatef(cv->rotation, 0.0f, 0.0f, 1.0f);
        rlTranslatef(-origin.x, -origin.y, 0.0f);

        BeginShaderMode(rs->fog_shader);
        DrawRectangle((int)inset, (int)inset,
                      (int)(cw - 2.0f * inset),
                      (int)(ch - 2.0f * inset), WHITE);
        EndShaderMode();

        rlPopMatrix();
    }
}

static void draw_button(const UIButton *btn, float ui_scale)
{
    if (!btn->visible) return;

    Color bg, text_col, border_col;

    if (btn->disabled) {
        bg = (Color){50, 50, 50, 255};
        text_col = (Color){120, 120, 120, 255};
        border_col = (Color){70, 70, 70, 255};
    } else if (btn->pressed) {
        bg = (Color){120, 60, 180, 255};
        text_col = WHITE;
        border_col = (Color){160, 80, 220, 255};
    } else if (btn->hovered) {
        bg = (Color){60, 140, 60, 255};
        text_col = WHITE;
        border_col = DARKGREEN;
    } else {
        bg = (Color){40, 120, 40, 255};
        text_col = WHITE;
        border_col = DARKGREEN;
    }

    DrawRectangleRounded(btn->bounds, 0.3f, 4, bg);
    DrawRectangleRoundedLines(btn->bounds, 0.3f, 4, border_col);

    int font_size = (int)(24.0f * ui_scale);
    int tw = MeasureText(btn->label, font_size);

    if (btn->subtitle != NULL) {
        int sub_size = (int)(14.0f * ui_scale);
        int sw = MeasureText(btn->subtitle, sub_size);
        int total_h = font_size + (int)(4.0f * ui_scale) + sub_size;
        int y_start = (int)(btn->bounds.y +
                            (btn->bounds.height - (float)total_h) * 0.5f);

        DrawText(btn->label,
                 (int)(btn->bounds.x + (btn->bounds.width - (float)tw) * 0.5f),
                 y_start, font_size, text_col);

        Color sub_col = btn->disabled ? (Color){100, 100, 100, 255} : LIGHTGRAY;
        DrawText(btn->subtitle,
                 (int)(btn->bounds.x + (btn->bounds.width - (float)sw) * 0.5f),
                 y_start + font_size + (int)(4.0f * ui_scale), sub_size, sub_col);
    } else {
        DrawText(btn->label,
                 (int)(btn->bounds.x + (btn->bounds.width - (float)tw) * 0.5f),
                 (int)(btn->bounds.y +
                       (btn->bounds.height - (float)font_size) * 0.5f),
                 font_size, text_col);
    }
}

static const char *pass_direction_string(PassDirection dir)
{
    switch (dir) {
    case PASS_LEFT:   return "Pass Left";
    case PASS_RIGHT:  return "Pass Right";
    case PASS_ACROSS: return "Pass Across";
    case PASS_NONE:   return "No Passing";
    default:          return "";
    }
}

static const char *player_name(int player_id)
{
    static const char *names[] = {"You", "West", "North", "East"};
    if (player_id >= 0 && player_id < NUM_PLAYERS) return names[player_id];
    return "???";
}

/* ---- Phase-specific drawing ---- */

/* ---- Login Screen ---- */

static void draw_phase_login(const GameState *gs, const RenderState *rs)
{
    (void)gs;
    const LoginUIState *lui = rs->login_ui;
    float s = rs->layout.scale;
    float cx = rs->layout.screen_width * 0.5f;
    float cy = rs->layout.screen_height * 0.5f;

    /* Title */
    const char *title = "HOLLOW HEARTS";
    int title_size = (int)(50.0f * s);
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(cx - (float)tw * 0.5f),
             (int)(cy - 160.0f * s), title_size, RAYWHITE);

    if (!lui) return;

    if (lui->show_username_input && !lui->awaiting_response) {
        /* Username text field */
        const char *prompt = "Choose a username:";
        int prompt_size = (int)(22.0f * s);
        int pw = MeasureText(prompt, prompt_size);
        DrawText(prompt, (int)(cx - (float)pw * 0.5f),
                 (int)(cy - 60.0f * s), prompt_size, LIGHTGRAY);

        float field_w = 300.0f * s;
        float field_h = 40.0f * s;
        float field_x = cx - field_w * 0.5f;
        float field_y = cy - 20.0f * s;
        DrawRectangle((int)field_x, (int)field_y,
                      (int)field_w, (int)field_h,
                      (Color){40, 40, 50, 255});
        DrawRectangleLines((int)field_x, (int)field_y,
                           (int)field_w, (int)field_h, LIGHTGRAY);

        int text_size = (int)(24.0f * s);
        float text_x = field_x + 8.0f * s;
        float text_y = field_y + (field_h - (float)text_size) * 0.5f;
        DrawText(lui->username_buf, (int)text_x, (int)text_y,
                 text_size, WHITE);

        /* Blinking cursor */
        if (lui->cursor_blink < 0.5f) {
            int text_w = MeasureText(lui->username_buf, text_size);
            DrawRectangle((int)(text_x + (float)text_w + 2.0f * s),
                          (int)text_y, (int)(2.0f * s), text_size, WHITE);
        }

        /* Register button */
        draw_button(&rs->btn_login_submit, s);

        /* Username rules hint */
        const char *hint = "3-31 characters, letters/numbers/underscore";
        int hint_size = (int)(14.0f * s);
        int hw = MeasureText(hint, hint_size);
        DrawText(hint, (int)(cx - (float)hw * 0.5f),
                 (int)(cy + 80.0f * s), hint_size, GRAY);
    } else if (lui->error_text[0]) {
        /* Error display */
        int err_size = (int)(20.0f * s);
        int ew = MeasureText(lui->error_text, err_size);
        DrawText(lui->error_text, (int)(cx - (float)ew * 0.5f),
                 (int)(cy - 20.0f * s), err_size, RED);
        draw_button(&rs->btn_login_retry, s);
    } else {
        /* Status text (Connecting... / Logging in...) */
        int status_size = (int)(22.0f * s);
        int stw = MeasureText(lui->status_text, status_size);
        DrawText(lui->status_text, (int)(cx - (float)stw * 0.5f),
                 (int)(cy - 10.0f * s), status_size, LIGHTGRAY);
    }
}

/* ---- Online Menu ---- */

static void draw_phase_online(const GameState *gs, const RenderState *rs)
{
    (void)gs;
    const OnlineUIState *oui = rs->online_ui;
    float s = rs->layout.scale;
    float cx = rs->layout.screen_width * 0.5f;
    float cy = rs->layout.screen_height * 0.5f;

    /* Title */
    const char *title = "PLAY ONLINE";
    int title_size = (int)(40.0f * s);
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(cx - (float)tw * 0.5f),
             (int)(cy - 180.0f * s), title_size, RAYWHITE);

    if (!oui) return;

    switch (oui->subphase) {
    case ONLINE_SUB_MENU:
        for (int i = 0; i < ONLINE_BTN_COUNT; i++)
            draw_button(&rs->online_btns[i], s);
        break;

    case ONLINE_SUB_CREATE_WAITING: {
        /* Room code large */
        const char *code_label = "Room Code:";
        int label_size = (int)(20.0f * s);
        int lw = MeasureText(code_label, label_size);
        DrawText(code_label, (int)(cx - (float)lw * 0.5f),
                 (int)(cy - 80.0f * s), label_size, LIGHTGRAY);

        int code_size = (int)(50.0f * s);
        int cw = MeasureText(oui->created_room_code, code_size);
        DrawText(oui->created_room_code, (int)(cx - (float)cw * 0.5f),
                 (int)(cy - 50.0f * s), code_size, GOLD);

        /* Player slots */
        int name_size = (int)(18.0f * s);
        float slot_y = cy + 10.0f * s;
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            char slot_text[48];
            Color slot_color;
            if (oui->slot_is_ai[i]) {
                snprintf(slot_text, sizeof(slot_text), "Seat %d: %s (AI)",
                         i + 1, oui->player_names[i]);
                slot_color = SKYBLUE;
            } else if (oui->player_names[i][0] != '\0') {
                snprintf(slot_text, sizeof(slot_text), "Seat %d: %s",
                         i + 1, oui->player_names[i]);
                slot_color = WHITE;
            } else {
                snprintf(slot_text, sizeof(slot_text), "Seat %d: ---", i + 1);
                slot_color = DARKGRAY;
            }
            int stw = MeasureText(slot_text, name_size);
            DrawText(slot_text, (int)(cx - (float)stw * 0.5f),
                     (int)slot_y, name_size, slot_color);
            slot_y += 24.0f * s;
        }

        /* Player count summary */
        char slots[32];
        snprintf(slots, sizeof(slots), "Players: %d / 4", oui->player_count);
        int slots_size = (int)(20.0f * s);
        int sw2 = MeasureText(slots, slots_size);
        DrawText(slots, (int)(cx - (float)sw2 * 0.5f),
                 (int)(slot_y + 8.0f * s), slots_size, LIGHTGRAY);

        const char *waiting = "Waiting for players...";
        int wait_size = (int)(16.0f * s);
        int ww = MeasureText(waiting, wait_size);
        DrawText(waiting, (int)(cx - (float)ww * 0.5f),
                 (int)(slot_y + 36.0f * s), wait_size, GRAY);

        draw_button(&rs->btn_online_add_ai, s);
        draw_button(&rs->btn_online_start_game, s);
        draw_button(&rs->btn_online_cancel, s);
        break;
    }

    case ONLINE_SUB_JOIN_INPUT: {
        const char *prompt = "Enter Room Code:";
        int prompt_size = (int)(22.0f * s);
        int pw = MeasureText(prompt, prompt_size);
        DrawText(prompt, (int)(cx - (float)pw * 0.5f),
                 (int)(cy - 60.0f * s), prompt_size, LIGHTGRAY);

        /* Text field */
        float field_w = 200.0f * s;
        float field_h = 50.0f * s;
        float field_x = cx - field_w * 0.5f;
        float field_y = cy - 25.0f * s;
        DrawRectangle((int)field_x, (int)field_y,
                      (int)field_w, (int)field_h,
                      (Color){40, 40, 50, 255});
        DrawRectangleLines((int)field_x, (int)field_y,
                           (int)field_w, (int)field_h, LIGHTGRAY);

        int text_size = (int)(30.0f * s);
        float text_x = field_x + 8.0f * s;
        float text_y = field_y + (field_h - (float)text_size) * 0.5f;
        DrawText(oui->room_code_buf, (int)text_x, (int)text_y,
                 text_size, WHITE);

        /* Blinking cursor */
        if (oui->cursor_blink < 0.5f) {
            int text_w = MeasureText(oui->room_code_buf, text_size);
            DrawRectangle((int)(text_x + (float)text_w + 2.0f * s),
                          (int)text_y, (int)(2.0f * s), text_size, WHITE);
        }

        draw_button(&rs->btn_online_join_submit, s);
        draw_button(&rs->btn_online_cancel, s);
        break;
    }

    case ONLINE_SUB_JOIN_WAITING: {
        const char *joining = "Joining room...";
        int join_size = (int)(24.0f * s);
        int jw = MeasureText(joining, join_size);
        DrawText(joining, (int)(cx - (float)jw * 0.5f),
                 (int)(cy - 20.0f * s), join_size, LIGHTGRAY);
        draw_button(&rs->btn_online_cancel, s);
        break;
    }

    case ONLINE_SUB_QUEUE_SEARCHING: {
        const char *searching = "Searching for match...";
        int search_size = (int)(24.0f * s);
        int sw2 = MeasureText(searching, search_size);
        DrawText(searching, (int)(cx - (float)sw2 * 0.5f),
                 (int)(cy - 20.0f * s), search_size, LIGHTGRAY);
        draw_button(&rs->btn_online_cancel, s);
        break;
    }

    case ONLINE_SUB_MATCH_FOUND: {
        const char *found = "Game Found!";
        int found_size = (int)(40.0f * s);
        int fw = MeasureText(found, found_size);
        DrawText(found, (int)(cx - (float)fw * 0.5f),
                 (int)(cy - 20.0f * s), found_size, GREEN);
        break;
    }

    case ONLINE_SUB_CONNECTING: {
        const char *connecting = "Connecting to server...";
        int conn_size = (int)(22.0f * s);
        int cw2 = MeasureText(connecting, conn_size);
        DrawText(connecting, (int)(cx - (float)cw2 * 0.5f),
                 (int)(cy - 10.0f * s), conn_size, LIGHTGRAY);
        break;
    }

    case ONLINE_SUB_CONNECTED_WAITING: {
        const char *waiting = "Connected — waiting for game...";
        int wait_size = (int)(22.0f * s);
        int ww = MeasureText(waiting, wait_size);
        DrawText(waiting, (int)(cx - (float)ww * 0.5f),
                 (int)(cy - 10.0f * s), wait_size, LIGHTGRAY);
        break;
    }

    case ONLINE_SUB_ERROR: {
        int err_size = (int)(20.0f * s);
        int ew = MeasureText(oui->error_text, err_size);
        DrawText(oui->error_text, (int)(cx - (float)ew * 0.5f),
                 (int)(cy - 20.0f * s), err_size, RED);
        draw_button(&rs->btn_online_cancel, s);
        break;
    }
    }
}

static void draw_phase_stats(const GameState *gs, const RenderState *rs)
{
    (void)gs;
    float s = rs->layout.scale;
    float cx = rs->layout.screen_width * 0.5f;
    float cy = rs->layout.screen_height * 0.5f;

    /* Title */
    const char *title = "My Stats";
    int title_size = (int)(40.0f * s);
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(cx - (float)tw * 0.5f),
             (int)(cy - 140.0f * s), title_size, RAYWHITE);

    if (!rs->stats_available) {
        const char *msg = "Log in to view stats";
        int msg_size = (int)(22.0f * s);
        int mw = MeasureText(msg, msg_size);
        DrawText(msg, (int)(cx - (float)mw * 0.5f),
                 (int)(cy - 20.0f * s), msg_size, LIGHTGRAY);
    } else {
        int label_size = (int)(22.0f * s);
        int value_size = (int)(28.0f * s);
        float row_y = cy - 80.0f * s;
        float row_spacing = 50.0f * s;

        struct { const char *label; char value[32]; } rows[4];
        snprintf(rows[0].value, sizeof(rows[0].value), "%d", rs->stat_elo);
        rows[0].label = "ELO Rating";
        snprintf(rows[1].value, sizeof(rows[1].value), "%u", rs->stat_games_played);
        rows[1].label = "Games Played";
        snprintf(rows[2].value, sizeof(rows[2].value), "%u", rs->stat_games_won);
        rows[2].label = "Games Won";
        float win_pct = rs->stat_games_played > 0
            ? 100.0f * (float)rs->stat_games_won / (float)rs->stat_games_played
            : 0.0f;
        snprintf(rows[3].value, sizeof(rows[3].value), "%.1f%%", win_pct);
        rows[3].label = "Win Rate";

        for (int i = 0; i < 4; i++) {
            int lw = MeasureText(rows[i].label, label_size);
            DrawText(rows[i].label, (int)(cx - (float)lw * 0.5f),
                     (int)row_y, label_size, LIGHTGRAY);
            int vw = MeasureText(rows[i].value, value_size);
            DrawText(rows[i].value, (int)(cx - (float)vw * 0.5f),
                     (int)(row_y + (float)label_size + 4.0f * s),
                     value_size, WHITE);
            row_y += row_spacing;
        }

        const char *note = "(updates on next login)";
        int note_size = (int)(14.0f * s);
        int nw = MeasureText(note, note_size);
        DrawText(note, (int)(cx - (float)nw * 0.5f),
                 (int)(row_y + 10.0f * s), note_size, GRAY);
    }

    draw_button(&rs->btn_stats_back, s);
}

static void draw_phase_menu(const GameState *gs, const RenderState *rs)
{
    (void)gs;
    float s = rs->layout.scale;
    float screen_cx = rs->layout.screen_width * 0.5f;

    float title_y = rs->menu_items[0].bounds.y - 90.0f * s;

    const char *title = "HOLLOW HEARTS";
    int title_size = (int)(50.0f * s);
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(screen_cx - (float)tw * 0.5f),
             (int)title_y, title_size, RAYWHITE);

    const char *subtitle = "A deck-building Hearts modification";
    int sub_size = (int)(20.0f * s);
    int sw = MeasureText(subtitle, sub_size);
    DrawText(subtitle, (int)(screen_cx - (float)sw * 0.5f),
             (int)(title_y + (float)title_size + 10.0f * s), sub_size, LIGHTGRAY);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        draw_button(&rs->menu_items[i], s);
    }
}

static void draw_contract_buttons(const RenderState *rs, float s)
{
    for (int i = 0; i < rs->contract_option_count; i++) {
        const UIButton *btn = &rs->contract_options[i];
        if (!btn->visible) continue;

        Color bg, text_col, border_col;
        if (i == rs->selected_contract_idx) {
            bg = (Color){180, 150, 30, 255};
            text_col = WHITE;
            border_col = GOLD;
        } else if (btn->hovered) {
            bg = (Color){70, 70, 90, 255};
            text_col = WHITE;
            border_col = LIGHTGRAY;
        } else {
            bg = (Color){50, 50, 70, 255};
            text_col = RAYWHITE;
            border_col = GRAY;
        }

        DrawRectangleRounded(btn->bounds, 0.1f, 4, bg);
        DrawRectangleRoundedLines(btn->bounds, 0.1f, 4, border_col);

        float bx = btn->bounds.x;
        float by = btn->bounds.y;
        float bw = btn->bounds.width;
        float pad = 8.0f * s;

        int cond_fs = (int)(13.0f * s);
        int name_fs = (int)(13.0f * s);
        int desc_fs = (int)(11.0f * s);
        float max_w = bw - pad * 2.0f;

        /* 1. Contract condition (label = description), centered word-wrap */
        float cy = by + pad;
        if (btn->label) {
            /* Measure to center: draw_text_wrapped is left-aligned,
             * so just draw from left padding */
            float h = draw_text_wrapped(btn->label, bx + pad, cy,
                                         cond_fs, max_w, text_col);
            cy += h + 4.0f * s;
        }

        /* 2. Transmutation card sprite, centered */
        int tid = rs->draft_transmute_ids[i];
        float card_scale = 0.65f * s;
        float card_w = CARD_WIDTH_REF * card_scale;
        float card_h = CARD_HEIGHT_REF * card_scale;
        float card_x = bx + (bw - card_w) * 0.5f;

        if (tid >= 0) {
            Vector2 pos = {card_x, cy};
            Vector2 origin = {0, 0};
            if (card_render_has_transmute_sprite(tid)) {
                card_render_transmute_face(tid, pos, card_scale,
                                           1.0f, false, false, 0.0f, origin);
            } else {
                /* Fallback: draw normal card face */
                const TransmutationDef *td = phase2_get_transmutation(tid);
                if (td) {
                    Card fallback = {.suit = td->result_suit, .rank = td->result_rank};
                    card_render_face(fallback, pos, card_scale,
                                     1.0f, false, false, 0.0f, origin);
                }
            }
        }
        cy += card_h + 4.0f * s;

        /* 3. Transmutation name (left-aligned, GOLD) */
        if (btn->subtitle && btn->subtitle[0] != '\0') {
            /* subtitle format: "TmuteName\nTmuteDescription" */
            const char *nl = strchr(btn->subtitle, '\n');
            if (nl) {
                int name_len = (int)(nl - btn->subtitle);
                char name_buf[64];
                int n = name_len < (int)sizeof(name_buf) - 1
                            ? name_len : (int)sizeof(name_buf) - 1;
                memcpy(name_buf, btn->subtitle, n);
                name_buf[n] = '\0';

                Color name_col = (i == rs->selected_contract_idx)
                                     ? (Color){255, 255, 200, 255} : GOLD;
                float h = draw_text_wrapped(name_buf, bx + pad, cy,
                                             name_fs, max_w, name_col);
                cy += h + 2.0f * s;

                /* 4. Transmutation description (left-aligned, LIGHTGRAY) */
                Color desc_col = (i == rs->selected_contract_idx)
                                     ? (Color){255, 255, 200, 200} : LIGHTGRAY;
                draw_text_wrapped(nl + 1, bx + pad, cy,
                                   desc_fs, max_w, desc_col);
            } else {
                Color name_col = (i == rs->selected_contract_idx)
                                     ? (Color){255, 255, 200, 255} : GOLD;
                draw_text_wrapped(btn->subtitle, bx + pad, cy,
                                   name_fs, max_w, name_col);
            }
        }
    }
}

static void draw_subphase_timer(const RenderState *rs, float s)
{
    int secs = (int)ceilf(rs->pass_subphase_remaining);
    if (secs < 0) secs = 0;

    char timer_text[16];
    snprintf(timer_text, sizeof(timer_text), "%d", secs);

    int timer_size = (int)(32.0f * s);
    Color timer_col = (secs <= 3) ? RED : GOLD;

    /* Top-right corner of screen (same padding as turn timer) */
    int tw = MeasureText(timer_text, timer_size);
    float bx = rs->layout.screen_width - (float)tw - 10.0f * s;
    float by = 6.0f * s;
    DrawText(timer_text, (int)bx, (int)by, timer_size, timer_col);
}

static void draw_phase_passing(const GameState *gs, const RenderState *rs)
{
    float s = rs->layout.scale;
    Vector2 bc = layout_board_center(&rs->layout);

    /* Subphase-specific content */
    switch (rs->pass_subphase) {
    case PASS_SUB_DEALER: {
        /* Status text */
        if (rs->pass_status_text) {
            int st_size = (int)(22.0f * s);
            int st_w = MeasureText(rs->pass_status_text, st_size);
            DrawText(rs->pass_status_text,
                     (int)(bc.x - (float)st_w * 0.5f),
                     (int)(bc.y - 120.0f * s), st_size, LIGHTGRAY);
        }
        if (rs->dealer_ui_active) {
            static const char *dir_labels[] = {"Left", "Front", "Right"};
            static const int dir_values[] = {PASS_LEFT, PASS_ACROSS, PASS_RIGHT};
            static const char *amt_labels[] = {"0", "2", "3", "4"};
            float btn_w = 90.0f * s;
            float btn_h = 36.0f * s;
            float gap = 10.0f * s;
            float total_w = 3.0f * btn_w + 2.0f * gap;
            float dir_y = bc.y - 60.0f * s;

            /* Direction label */
            const char *dir_lbl = "Direction:";
            int dlbl_size = (int)(16.0f * s);
            int dlbl_w = MeasureText(dir_lbl, dlbl_size);
            DrawText(dir_lbl, (int)(bc.x - (float)dlbl_w * 0.5f),
                     (int)(dir_y - 22.0f * s), dlbl_size, GRAY);

            for (int i = 0; i < DEALER_DIR_BTN_COUNT; i++) {
                float bx2 = bc.x - total_w * 0.5f + (float)i * (btn_w + gap);
                Rectangle r = {bx2, dir_y, btn_w, btn_h};
                bool selected = (rs->dealer_selected_dir == dir_values[i]);
                Color bg = selected ? (Color){60, 120, 80, 255}
                                    : (Color){40, 40, 50, 220};
                Color border = selected ? GREEN : GRAY;
                DrawRectangleRec(r, bg);
                DrawRectangleLinesEx(r, 2.0f, border);
                int lbl_size = (int)(18.0f * s);
                int lw = MeasureText(dir_labels[i], lbl_size);
                DrawText(dir_labels[i],
                         (int)(bx2 + (btn_w - (float)lw) * 0.5f),
                         (int)(dir_y + (btn_h - (float)lbl_size) * 0.5f),
                         lbl_size, WHITE);
            }

            /* Amount buttons */
            float amt_y = bc.y + 10.0f * s;
            float amt_w = 60.0f * s;
            float amt_total = 4.0f * amt_w + 3.0f * gap;

            const char *amt_lbl = "Cards to pass:";
            int albl_size = (int)(16.0f * s);
            int albl_w = MeasureText(amt_lbl, albl_size);
            DrawText(amt_lbl, (int)(bc.x - (float)albl_w * 0.5f),
                     (int)(amt_y - 22.0f * s), albl_size, GRAY);

            for (int i = 0; i < DEALER_AMT_BTN_COUNT; i++) {
                float bx2 = bc.x - amt_total * 0.5f + (float)i * (amt_w + gap);
                Rectangle r = {bx2, amt_y, amt_w, btn_h};
                bool selected = (rs->dealer_selected_amt == i);
                Color bg = selected ? (Color){60, 120, 80, 255}
                                    : (Color){40, 40, 50, 220};
                Color border = selected ? GREEN : GRAY;
                DrawRectangleRec(r, bg);
                DrawRectangleLinesEx(r, 2.0f, border);
                int lbl_size = (int)(18.0f * s);
                int lw = MeasureText(amt_labels[i], lbl_size);
                DrawText(amt_labels[i],
                         (int)(bx2 + (amt_w - (float)lw) * 0.5f),
                         (int)(amt_y + (btn_h - (float)lbl_size) * 0.5f),
                         lbl_size, WHITE);
            }

            /* Confirm button */
            float cfm_w = 120.0f * s;
            float cfm_y = bc.y + 80.0f * s;
            Rectangle cfm_r = {bc.x - cfm_w * 0.5f, cfm_y, cfm_w, btn_h};
            DrawRectangleRec(cfm_r, (Color){60, 60, 100, 220});
            DrawRectangleLinesEx(cfm_r, 2.0f, SKYBLUE);
            const char *cfm_lbl = "Confirm";
            int cfm_size = (int)(18.0f * s);
            int cfm_lw = MeasureText(cfm_lbl, cfm_size);
            DrawText(cfm_lbl,
                     (int)(bc.x - (float)cfm_lw * 0.5f),
                     (int)(cfm_y + (btn_h - (float)cfm_size) * 0.5f),
                     cfm_size, WHITE);
        }
        draw_subphase_timer(rs, s);
        break;
    }

    case PASS_SUB_CONTRACT:
        /* Label */
        if (rs->pass_status_text) {
            int ctr_size = (int)(20.0f * s);
            int ctr_w = MeasureText(rs->pass_status_text, ctr_size);
            float label_y = (rs->contract_option_count > 0)
                                ? rs->contract_options[0].bounds.y - 28.0f * s
                                : bc.y - 140.0f * s;
            DrawText(rs->pass_status_text,
                     (int)(bc.x - (float)ctr_w * 0.5f),
                     (int)label_y, ctr_size, LIGHTGRAY);
        }
        if (rs->contract_ui_active) {
            draw_contract_buttons(rs, s);
        }
        draw_subphase_timer(rs, s);
        break;

    case PASS_SUB_CARD_PASS: {
        /* Pass direction indicator */
        Vector2 dir_pos = layout_pass_direction_position(&rs->layout);
        const char *dir_str = pass_direction_string(gs->pass_direction);
        int dir_size = (int)(28.0f * s);
        int dw = MeasureText(dir_str, dir_size);
        DrawText(dir_str, (int)(dir_pos.x + (160.0f * s - (float)dw) * 0.5f),
                 (int)dir_pos.y, dir_size, GOLD);

        /* Selection count */
        char sel_text[32];
        snprintf(sel_text, sizeof(sel_text), "Selected: %d / %d",
                 rs->selected_count, gs->pass_card_count);
        int sel_size = (int)(20.0f * s);
        int sel_w = MeasureText(sel_text, sel_size);
        DrawText(sel_text, (int)(bc.x - (float)sel_w * 0.5f),
                 (int)(rs->layout.board_y + 270.0f * s), sel_size, LIGHTGRAY);

        draw_subphase_timer(rs, s);
        draw_button(&rs->btn_confirm_pass, s);
        break;
    }

    case PASS_SUB_TOSS_ANIM:
    case PASS_SUB_TOSS_WAIT:
    case PASS_SUB_REVEAL:
    case PASS_SUB_RECEIVE:
        /* No UI overlay during pass animation — just show cards */
        break;

    default:
        break;
    }

    /* Draw all hands (all subphases) */
    for (int i = 0; i < rs->card_count; i++) {
        draw_card_visual(&rs->cards[i], s, rs);
    }
}

/* ---- Left panel drawing ---- */

static void draw_left_panel_chat(const RenderState *rs)
{
    const LayoutConfig *cfg = &rs->layout;
    if (cfg->board_x < 40.0f * cfg->scale) return;

    Rectangle r = layout_left_panel_upper(cfg);
    float s = cfg->scale;

    DrawRectangleRec(r, (Color){15, 15, 25, 200});

    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);

    int msg_fs = (int)(13.0f * s);
    float text_x = r.x + 6.0f * s;
    float max_w = r.width - 12.0f * s;

    float content_top = r.y + 4.0f * s;
    float content_bot = r.y + r.height - 4.0f * s;
    float avail_h = content_bot - content_top;

    /* Measure wrapped heights from newest to oldest to find which
     * messages fit in the visible area. */
    int count = rs->chat_count;
    float total_h = 0.0f;
    int first_visible = count; /* index into logical order */
    for (int i = count - 1; i >= 0; i--) {
        int ring_idx = (rs->chat_head + i) % CHAT_LOG_MAX;
        float h = measure_text_wrapped(rs->chat_msgs[ring_idx],
                                       msg_fs, max_w);
        if (total_h + h > avail_h) break;
        total_h += h;
        first_visible = i;
    }

    /* Draw visible messages top-to-bottom */
    float y = content_bot - total_h;
    for (int i = first_visible; i < count; i++) {
        int ring_idx = (rs->chat_head + i) % CHAT_LOG_MAX;
        float h = draw_text_wrapped(rs->chat_msgs[ring_idx],
                                    text_x, y, msg_fs, max_w,
                                    rs->chat_colors[ring_idx]);
        y += h;
    }

    EndScissorMode();
}

static void draw_left_panel_info(const RenderState *rs)
{
    const LayoutConfig *cfg = &rs->layout;
    if (cfg->board_x < 40.0f * cfg->scale) return;

    Rectangle r = layout_left_panel_lower(cfg);
    float s = cfg->scale;

    DrawRectangleRec(r, (Color){15, 25, 15, 200});

    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);

    int header_fs = (int)(14.0f * s);
    int body_fs = (int)(12.0f * s);
    float pad = 6.0f * s;
    float x = r.x + pad;
    float y = r.y + pad;
    float max_w = r.width - pad * 2;

    /* Contract section */
    DrawText("Contracts", (int)x, (int)y, header_fs, GOLD);
    y += (float)header_fs + 4.0f * s;
    if (rs->info_contract_count > 0) {
        for (int c = 0; c < rs->info_contract_count; c++) {
            y += draw_text_wrapped(rs->info_contract_name[c], x, y,
                                   body_fs, max_w, WHITE);
            y += draw_text_wrapped(rs->info_contract_desc[c], x, y,
                                   body_fs, max_w, LIGHTGRAY);
            y += 2.0f * s;
        }
    } else {
        DrawText("None", (int)x, (int)y, body_fs, GRAY);
        y += (float)body_fs + 2.0f * s;
    }

    y += 6.0f * s;

    /* Transmutation inventory section */
    if (rs->transmute_btn_count > 0) {
        DrawText("Transmutations", (int)x, (int)y, header_fs, GOLD);
        y += (float)header_fs + 4.0f * s;

        if (rs->pending_transmutation_id >= 0) {
            DrawText("Click a card to apply", (int)x, (int)y, body_fs, YELLOW);
            y += (float)body_fs + 4.0f * s;
        }

        for (int i = 0; i < rs->transmute_btn_count; i++) {
            draw_button(&rs->transmute_btns[i], s);
        }
        /* Advance y past buttons */
        const UIButton *last = &rs->transmute_btns[rs->transmute_btn_count - 1];
        y = last->bounds.y + last->bounds.height + 6.0f * s;
    }

    /* Transmutation card descriptions */
    if (rs->transmute_info_count > 0) {
        y += 2.0f * s;
        DrawText("Card Effects", (int)x, (int)y, header_fs, GOLD);
        y += (float)header_fs + 4.0f * s;
        for (int i = 0; i < rs->transmute_info_count; i++) {
            y += draw_text_wrapped(rs->transmute_info_text[i], x, y,
                                   body_fs, max_w,
                                   (Color){200, 160, 255, 255});
        }
    }

    y += 6.0f * s;

    EndScissorMode();
}

static void draw_phase_playing(const GameState *gs, const RenderState *rs)
{
    float s = rs->layout.scale;
    const LayoutConfig *cfg = &rs->layout;

    /* Left column panels */
    draw_left_panel_chat(rs);
    draw_left_panel_info(rs);

    /* Draw pile cards (underneath trick/hand cards) */
    for (int i = 0; i < rs->pile_card_count; i++) {
        draw_card_visual(&rs->pile_cards[i], s, rs);
    }

    /* Draw hands */
    for (int i = 0; i < rs->card_count; i++) {
        draw_card_visual(&rs->cards[i], s, rs);
    }

    /* Shield indicators next to player piles */
    for (int p = 0; p < NUM_PLAYERS; p++) {
        if (rs->shield_remaining[p] <= 0) continue;
        PlayerPosition spos = player_screen_pos(p);
        Vector2 pile = layout_pile_position(spos, cfg);
        float r = 14.0f * s; /* shield radius */
        float ox, oy;
        switch (spos) {
        case POS_BOTTOM: ox = pile.x + 65.0f * s; oy = pile.y;              break; /* right */
        case POS_TOP:    ox = pile.x - 65.0f * s; oy = pile.y;              break; /* left */
        case POS_RIGHT:  ox = pile.x;              oy = pile.y - 70.0f * s; break; /* top */
        case POS_LEFT:   ox = pile.x;              oy = pile.y + 70.0f * s; break; /* bottom */
        default:         ox = pile.x;              oy = pile.y;        break;
        }
        /* Shield shape: flat top, vertical sides, angled point at bottom */
        float hw = r * 0.7f;        /* half width */
        float top_y = oy - r;       /* top edge */
        float mid_y = oy + r * 0.3f; /* where sides end and angle begins */
        float bot_y = oy + r;       /* bottom tip */
        Vector2 tl = {ox - hw, top_y};
        Vector2 tr = {ox + hw, top_y};
        Vector2 br = {ox + hw, mid_y};
        Vector2 bl = {ox - hw, mid_y};
        Vector2 tip = {ox, bot_y};
        Color fill = (Color){220, 190, 50, 220};
        Color edge = (Color){220, 190, 50, 255};
        float lw = 2.0f * s; /* line thickness */
        /* Fill: rectangle top + triangle bottom */
        DrawTriangle(tl, tr, bl, fill);
        DrawTriangle(bl, tr, br, fill);
        DrawTriangle(bl, br, tip, fill);
        /* Thick outline */
        DrawLineEx(tl, tr, lw, edge);  /* top */
        DrawLineEx(tr, br, lw, edge);  /* right side */
        DrawLineEx(br, tip, lw, edge); /* right angle */
        DrawLineEx(tip, bl, lw, edge); /* left angle */
        DrawLineEx(bl, tl, lw, edge);  /* left side */
        /* Number in shield color */
        char shield_txt[4];
        snprintf(shield_txt, sizeof(shield_txt), "%d", rs->shield_remaining[p]);
        int stfs = (int)(13.0f * s);
        int stw = MeasureText(shield_txt, stfs);
        float text_cy = (top_y + mid_y) * 0.5f + 2.0f * s; /* slightly below rect center */
        DrawText(shield_txt, (int)(ox - (float)stw * 0.5f),
                 (int)(text_cy - (float)stfs * 0.5f), stfs, edge);
    }

    /* Turn indicator + timer (top-right of board) */
    int current = game_state_current_player(gs);
    if (current >= 0) {
        char turn_text[48];
        if (current == HUMAN_PLAYER) {
            snprintf(turn_text, sizeof(turn_text), "Your turn");
        } else {
            snprintf(turn_text, sizeof(turn_text), "%s's turn",
                     player_name(current));
        }

        int secs = (int)ceilf(rs->turn_time_remaining);
        if (secs < 0) secs = 0;

        char full_text[64];
        snprintf(full_text, sizeof(full_text), "%s  %d", turn_text, secs);

        int timer_size = (int)(24.0f * s);
        Color col = (secs <= 5) ? RED : (current == HUMAN_PLAYER ? GREEN : GOLD);
        int tw = MeasureText(full_text, timer_size);
        float tx = cfg->screen_width - (float)tw - 10.0f * s;
        float ty = 6.0f * s;
        DrawText(full_text, (int)tx, (int)ty, timer_size, col);
    }

    /* Trick count */
    char trick_text[32];
    snprintf(trick_text, sizeof(trick_text), "Trick %d/13", gs->tricks_played + 1);
    int trick_size = (int)(18.0f * s);
    int trick_tw = MeasureText(trick_text, trick_size);
    DrawText(trick_text,
             (int)(cfg->screen_width - (float)trick_tw - 10.0f * s),
             (int)(cfg->board_y + 38.0f * s), trick_size, LIGHTGRAY);

    /* Dim unplayable cards — handled inside draw_card_visual via cv->dimmed */
}

/* Draw the contracts results panel: 3 columns (Player | Reward | Contract).
 * Only completed contracts are shown. Scrollable when content overflows. */
static void draw_contracts_panel(const RenderState *rs, float s,
                                  const LayoutConfig *cfg)
{
    ContractsTableLayout tbl;
    layout_contracts_table(cfg, &tbl);

    Vector2 bc = layout_board_center(cfg);
    int font22 = (int)(22.0f * s);
    int font13 = (int)(13.0f * s);
    int font11 = (int)(11.0f * s);
    int table_x = (int)tbl.table_x;
    int col2_x  = (int)tbl.col2_x;
    int col3_x  = (int)tbl.col3_x;
    int col3_w  = (int)tbl.col3_w;
    float col2_w = (float)col3_x - (float)col2_x - 8.0f * s;

    /* Title */
    const char *title = "Rewards Obtained";
    int title_size = (int)(36.0f * s);
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(bc.x - (float)tw * 0.5f),
             (int)tbl.title_y, title_size, GOLD);

    /* Header */
    int header_y = (int)tbl.header_y;
    DrawText("Player", table_x, header_y, font22, LIGHTGRAY);
    DrawText("Reward", col2_x, header_y, font22, LIGHTGRAY);
    DrawText("Contract", col3_x, header_y, font22, LIGHTGRAY);

    DrawLine(table_x, (int)tbl.line_y,
             table_x + (int)tbl.table_w, (int)tbl.line_y, GRAY);

    if (rs->contract_result_count == 0 && rs->contract_reveal_count > 0) {
        int y = (int)tbl.first_row_y;
        DrawText("No contracts completed this round", table_x, y, font22, GRAY);
        return;
    }

    /* Compute available scroll area */
    Rectangle br = layout_board_rect(cfg);
    float scroll_top = tbl.first_row_y;
    float scroll_bottom = br.y + br.height - 150.0f * s; /* leave room for button + indicator */
    float visible_h = scroll_bottom - scroll_top;

    /* Handle mouse wheel scroll (mutable cast for scroll state) */
    float *scroll = (float *)&rs->contract_scroll_y;
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        *scroll -= wheel * 30.0f * s;
        if (*scroll < 0.0f) *scroll = 0.0f;
    }

    /* Compute total content height */
    float total_h = tbl.row_h * (float)rs->contract_result_count;
    float max_scroll = total_h - visible_h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (*scroll > max_scroll) *scroll = max_scroll;

    /* Clip rows to visible area */
    BeginScissorMode((int)br.x, (int)scroll_top,
                     (int)br.width, (int)visible_h);

    /* Completed contract rows */
    for (int i = 0; i < rs->contract_result_count; i++) {
        if (rs->contract_result_name[i][0] == '\0') continue;

        float fy = tbl.first_row_y + tbl.row_h * (float)i - rs->contract_scroll_y;
        int y = (int)fy;
        bool revealed = i < rs->contract_reveal_count;

        if (revealed) {
            Color row_col = GREEN;
            Color tdesc_col = (Color){100, 200, 100, 180};

            /* Column 1: Player name */
            DrawText(rs->contract_result_text[i], table_x, y, font22, row_col);

            /* Column 2: Transmutation name + transmutation description */
            DrawText(rs->contract_result_name[i], col2_x, y, font22, row_col);
            draw_text_wrapped(rs->contract_result_tdesc[i],
                              (float)col2_x, (float)(y + font22 + (int)(2.0f * s)),
                              font11, col2_w, tdesc_col);

            /* Column 3: Contract condition */
            draw_text_wrapped(rs->contract_result_desc[i],
                              (float)col3_x, (float)y,
                              font13, (float)col3_w, LIGHTGRAY);
        } else {
            /* Not yet revealed */
            DrawText(rs->contract_result_text[i], table_x, y, font22, LIGHTGRAY);
            DrawText(rs->contract_result_name[i], col2_x, y, font22, LIGHTGRAY);
            draw_text_wrapped(rs->contract_result_desc[i],
                              (float)col3_x, (float)y,
                              font13, (float)col3_w, DARKGRAY);
        }
    }

    EndScissorMode();

    /* Scroll-down indicator (between table bottom and continue button) */
    if (max_scroll > 0.0f && *scroll < max_scroll - 1.0f) {
        float arrow_cx = tbl.table_x + tbl.table_w * 0.5f;
        float arrow_y = scroll_bottom + 8.0f * s;
        float arrow_sz = 10.0f * s;
        float pulse = 0.6f + 0.4f * sinf((float)GetTime() * 3.0f);
        unsigned char alpha = (unsigned char)(255.0f * pulse);

        /* Down arrow triangle (counter-clockwise for Raylib) */
        Color arrow_col = (Color){200, 200, 200, alpha};
        Vector2 v1 = {arrow_cx + arrow_sz, arrow_y};
        Vector2 v2 = {arrow_cx - arrow_sz, arrow_y};
        Vector2 v3 = {arrow_cx, arrow_y + arrow_sz};
        DrawTriangle(v1, v2, v3, arrow_col);
    }
}

/* Draw the scoring table (shared by several subphases). */
static void draw_scoring_table(const RenderState *rs, float s,
                                const LayoutConfig *cfg)
{
    ScoringTableLayout tbl;
    layout_scoring_table(cfg, rs->score_menu_slide_y, &tbl);

    Rectangle br = layout_board_rect(cfg);

    /* Clip the sliding menu content to the board area */
    BeginScissorMode((int)br.x, (int)br.y, (int)br.width, (int)br.height);

    Vector2 bc = layout_board_center(cfg);
    int col_w = (int)tbl.col_w;
    int table_x = (int)tbl.table_x;
    int table_y = (int)tbl.header_y;
    int font22 = (int)(22.0f * s);

    /* Title */
    const char *title = "Round Complete";
    int title_size = (int)(36.0f * s);
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(bc.x - (float)tw * 0.5f),
             (int)tbl.title_y, title_size, RAYWHITE);

    /* Header */
    DrawText("Player", table_x, table_y, font22, LIGHTGRAY);
    DrawText("Total", table_x + col_w, table_y, font22, LIGHTGRAY);
    DrawText("Round", table_x + col_w * 2, table_y, font22, LIGHTGRAY);
    DrawText("Cards", table_x + col_w * 3, table_y, font22, LIGHTGRAY);

    DrawLine(table_x, (int)tbl.line_y,
             table_x + col_w * tbl.num_cols, (int)tbl.line_y, GRAY);

    /* Find best/worst for highlighting (use displayed totals for animation) */
    int best_score = 999, worst_score = -1;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        int score = rs->displayed_total_scores[i];
        if (score < best_score) best_score = score;
        if (score > worst_score) worst_score = score;
    }

    for (int i = 0; i < NUM_PLAYERS; i++) {
        int y = (int)layout_scoring_row_y(i, &tbl);
        Color row_col = RAYWHITE;
        if (rs->displayed_total_scores[i] == best_score) row_col = GREEN;
        if (rs->displayed_total_scores[i] == worst_score) row_col = RED;

        DrawText(player_name(i), table_x, y, font22, row_col);

        char pts[16];
        snprintf(pts, sizeof(pts), "%d", rs->displayed_total_scores[i]);
        DrawText(pts, table_x + col_w, y, font22, row_col);

        snprintf(pts, sizeof(pts), "+%d", rs->displayed_round_points[i]);
        DrawText(pts, table_x + col_w * 2, y, font22, row_col);
    }

    EndScissorMode();
}

static void draw_phase_scoring(const GameState *gs, const RenderState *rs)
{
    (void)gs;
    float s = rs->layout.scale;
    const LayoutConfig *cfg = &rs->layout;
    Rectangle br = layout_board_rect(cfg);

    /* Dark overlay */
    DrawRectangleRec(br, (Color){0, 0, 0, 150});

    if (rs->score_subphase == SCORE_SUB_CONTRACTS) {
        /* Contracts results panel (handles its own scissor clipping) */
        draw_contracts_panel(rs, s, cfg);
    } else {
        /* Scoring table */
        draw_scoring_table(rs, s, cfg);

        /* Draw scoring pile cards (flying cards drawn on top of overlay) */
        card_render_set_filter(TEXTURE_FILTER_BILINEAR);
        for (int i = 0; i < rs->pile_card_count; i++) {
            if (rs->pile_cards[i].opacity > 0.0f) {
                draw_card_visual(&rs->pile_cards[i], s, rs);
            }
        }
        card_render_set_filter(TEXTURE_FILTER_POINT);
    }

    /* Buttons drawn outside scissor so they're always visible */
    draw_button(&rs->btn_continue, s);

    /* Online auto-advance timer (top-right corner) */
    if (rs->online && rs->score_auto_timer > 0.0f) {
        bool awaiting =
            (rs->score_subphase == SCORE_SUB_DISPLAY) ||
            (rs->score_subphase == SCORE_SUB_DONE) ||
            (rs->score_subphase == SCORE_SUB_CONTRACTS &&
             rs->contract_reveal_count >= rs->contract_result_count);
        if (awaiting) {
            int secs = (int)ceilf(15.0f - rs->score_auto_timer);
            if (secs < 0) secs = 0;
            char timer_text[16];
            snprintf(timer_text, sizeof(timer_text), "%d", secs);
            int timer_size = (int)(32.0f * s);
            Color timer_col = (secs <= 3) ? RED : GOLD;
            int tw = MeasureText(timer_text, timer_size);
            float bx = cfg->screen_width - (float)tw - 10.0f * s;
            float by = 6.0f * s;
            DrawText(timer_text, (int)bx, (int)by, timer_size, timer_col);
        }
    }
}

static void draw_phase_game_over(const GameState *gs, const RenderState *rs)
{
    float s = rs->layout.scale;
    const LayoutConfig *cfg = &rs->layout;

    /* Full-screen dark overlay (no left panel on game over) */
    DrawRectangle(0, 0, (int)cfg->screen_width, (int)cfg->screen_height,
                  (Color){0, 0, 0, 200});

    float cx = cfg->screen_width * 0.5f;
    float cy = cfg->screen_height * 0.5f;

    const char *title = "Game Over";
    int title_size = (int)(48.0f * s);
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(cx - (float)tw * 0.5f),
             (int)(cy - 180.0f * s), title_size, GOLD);

    /* Winner announcement */
    int winners[NUM_PLAYERS];
    int win_count = game_state_get_winners(gs, winners);

    char winner_text[128];
    if (win_count == 1) {
        snprintf(winner_text, sizeof(winner_text), "%s wins!",
                 player_name(winners[0]));
    } else {
        snprintf(winner_text, sizeof(winner_text), "Tie!");
    }
    int win_size = (int)(36.0f * s);
    int ww = MeasureText(winner_text, win_size);
    DrawText(winner_text, (int)(cx - (float)ww * 0.5f),
             (int)(cy - 100.0f * s), win_size, RAYWHITE);

    /* Final scores */
    int score_col_w = (int)(150.0f * s);
    int table_w = (int)(300.0f * s);
    int table_x = (int)(cx - (float)table_w * 0.5f);
    int table_y = (int)(cy - 20.0f * s);
    int row_h = (int)(35.0f * s);
    int font22 = (int)(22.0f * s);

    DrawText("Player", table_x, table_y, font22, LIGHTGRAY);
    DrawText("Score", table_x + score_col_w, table_y, font22, LIGHTGRAY);
    DrawLine(table_x, table_y + (int)(28.0f * s),
             table_x + table_w, table_y + (int)(28.0f * s), GRAY);

    for (int i = 0; i < NUM_PLAYERS; i++) {
        int y = table_y + row_h * (i + 1);
        Color col = RAYWHITE;
        for (int w = 0; w < win_count; w++) {
            if (winners[w] == i) { col = GREEN; break; }
        }

        DrawText(player_name(i), table_x, y, font22, col);

        char pts[16];
        snprintf(pts, sizeof(pts), "%d", gs->players[i].total_score);
        DrawText(pts, table_x + score_col_w, y, font22, col);
    }

    draw_button(&rs->btn_continue, s);
}

static void draw_phase_settings(const GameState *gs, const RenderState *rs)
{
    (void)gs;
    float s = rs->layout.scale;
    float screen_cx = rs->layout.screen_width * 0.5f;

    /* Title */
    const char *title = "SETTINGS";
    int title_size = (int)(40.0f * s);
    int tw = MeasureText(title, title_size);
    float title_y = rs->settings_tab_btns[0].bounds.y - 50.0f * s;
    DrawText(title, (int)(screen_cx - (float)tw * 0.5f),
             (int)title_y, title_size, RAYWHITE);

    /* Draw tab bar */
    int tab_fs = (int)(18.0f * s);
    for (int t = 0; t < SETTINGS_TAB_COUNT; t++) {
        const UIButton *tab = &rs->settings_tab_btns[t];
        bool active = (t == (int)rs->settings_tab);
        Rectangle r = tab->bounds;

        /* Tab background */
        Color bg = active ? (Color){60, 60, 60, 255} : (Color){35, 35, 35, 255};
        if (tab->hovered && !active) bg = (Color){50, 50, 50, 255};
        DrawRectangleRec(r, bg);

        /* Active tab underline */
        if (active) {
            DrawRectangle((int)r.x, (int)(r.y + r.height - 3.0f * s),
                          (int)r.width, (int)(3.0f * s), GOLD);
        }

        /* Tab label */
        Color lbl = active ? GOLD : LIGHTGRAY;
        int lw = MeasureText(tab->label, tab_fs);
        DrawText(tab->label,
                 (int)(r.x + (r.width - (float)lw) * 0.5f),
                 (int)(r.y + (r.height - (float)tab_fs) * 0.5f),
                 tab_fs, lbl);
    }

    /* Draw setting rows (only visible ones for active tab) */
    int label_fs = (int)(20.0f * s);
    int value_fs = (int)(20.0f * s);
    float row_h = 40.0f * s;
    float label_x = screen_cx - 230.0f * s;

    float arrow_sz = 30.0f * s;
    for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
        if (!rs->settings_rows_prev[i].visible) continue;

        /* Recover row top from arrow button position */
        float row_top = rs->settings_rows_prev[i].bounds.y - (row_h - arrow_sz) * 0.5f;

        Color lbl_col = rs->settings_disabled[i]
                            ? (Color){100, 100, 100, 255}
                            : RAYWHITE;
        Color val_col = rs->settings_disabled[i]
                            ? (Color){80, 80, 80, 255}
                            : GOLD;

        /* Label */
        DrawText(rs->settings_labels[i],
                 (int)label_x,
                 (int)(row_top + (row_h - (float)label_fs) * 0.5f),
                 label_fs, lbl_col);

        /* Value text centered between arrows */
        const char *val = rs->settings_value_bufs[i];
        if (val[0] != '\0') {
            int vw = MeasureText(val, value_fs);
            float arrow_left = rs->settings_rows_prev[i].bounds.x +
                               rs->settings_rows_prev[i].bounds.width;
            float arrow_right = rs->settings_rows_next[i].bounds.x;
            float center_x = (arrow_left + arrow_right) * 0.5f;
            DrawText(val, (int)(center_x - (float)vw * 0.5f),
                     (int)(row_top + (row_h - (float)value_fs) * 0.5f),
                     value_fs, val_col);
        }

        /* Arrow buttons */
        draw_button(&rs->settings_rows_prev[i], s);
        draw_button(&rs->settings_rows_next[i], s);
    }

    /* Apply and Back buttons */
    draw_button(&rs->btn_settings_apply, s);
    draw_button(&rs->btn_settings_back, s);
}

/* ---- Pause overlay ---- */

static void draw_pause_overlay(const RenderState *rs)
{
    float s = rs->layout.scale;
    const LayoutConfig *cfg = &rs->layout;
    float cx = cfg->screen_width * 0.5f;

    /* Dark overlay */
    DrawRectangle(0, 0, (int)cfg->screen_width, (int)cfg->screen_height,
                  (Color){0, 0, 0, 200});

    if (rs->pause_state == PAUSE_MENU) {
        const char *title = rs->online ? "MENU" : "PAUSED";
        int title_size = (int)(48.0f * s);
        int tw = MeasureText(title, title_size);
        float title_y = rs->pause_btns[0].bounds.y - 60.0f * s;
        DrawText(title, (int)(cx - (float)tw * 0.5f),
                 (int)title_y, title_size, GOLD);

        /* 4 buttons */
        for (int i = 0; i < PAUSE_BTN_COUNT; i++)
            draw_button(&rs->pause_btns[i], s);
    } else {
        /* Confirmation dialog */
        const char *prompt = (rs->pause_state == PAUSE_CONFIRM_MENU)
                                 ? "Return to Main Menu?"
                                 : "Quit Game?";
        int prompt_size = (int)(36.0f * s);
        int pw = MeasureText(prompt, prompt_size);
        float prompt_y = rs->pause_confirm_yes.bounds.y - 60.0f * s;
        DrawText(prompt, (int)(cx - (float)pw * 0.5f),
                 (int)prompt_y, prompt_size, RAYWHITE);

        draw_button(&rs->pause_confirm_yes, s);
        draw_button(&rs->pause_confirm_no, s);
    }
}

/* Draw the game scene for a given in-game phase (used to show game behind overlays). */
static void draw_ingame_phase(const GameState *gs, const RenderState *rs,
                               GamePhase phase)
{
    switch (phase) {
    case PHASE_DEALING: {
        float s = rs->layout.scale;
        Vector2 bc = layout_board_center(&rs->layout);

        draw_left_panel_chat(rs);
        draw_left_panel_info(rs);

        int cards_in_flight = 0;
        for (int i = 0; i < rs->card_count; i++) {
            if (rs->cards[i].animating || rs->cards[i].anim_delay > 0.0f)
                cards_in_flight++;
        }
        int deck_remaining = 52 - (rs->card_count - cards_in_flight);
        if (deck_remaining < 0) deck_remaining = 0;
        int deck_layers = (deck_remaining + 6) / 7;
        if (deck_layers > 7) deck_layers = 7;
        Vector2 deck_origin = {rs->layout.card_width * 0.5f, rs->layout.card_height * 0.5f};
        for (int i = 0; i < deck_layers; i++) {
            Vector2 pos = {bc.x - (float)i * 0.5f, bc.y - (float)i * 0.5f};
            card_render_back(pos, s, 1.0f, 0.0f, deck_origin);
        }

        {
            int sorted[MAX_CARD_VISUALS];
            int n = rs->card_count;
            for (int i = 0; i < n; i++) sorted[i] = i;
            for (int i = 1; i < n; i++) {
                int key = sorted[i];
                int kz = rs->cards[key].z_order;
                int j = i - 1;
                while (j >= 0 && rs->cards[sorted[j]].z_order > kz) {
                    sorted[j + 1] = sorted[j];
                    j--;
                }
                sorted[j + 1] = key;
            }
            for (int i = 0; i < n; i++) {
                draw_card_visual(&rs->cards[sorted[i]], s, rs);
            }
        }

        if (!rs->deal_complete) {
            const char *deal_text = "Dealing...";
            int deal_size = (int)(22.0f * s);
            int dw = MeasureText(deal_text, deal_size);
            DrawText(deal_text, (int)(bc.x - (float)dw * 0.5f),
                     (int)(bc.y + rs->layout.card_height * 0.5f + 20.0f * s),
                     deal_size, LIGHTGRAY);
        }

        char round_text[32];
        snprintf(round_text, sizeof(round_text), "Round %d", gs->round_number);
        int round_size = (int)(30.0f * s);
        int rw = MeasureText(round_text, round_size);
        DrawText(round_text, (int)(bc.x - (float)rw * 0.5f),
                 (int)(rs->layout.board_y + 20.0f * s), round_size, GOLD);
        break;
    }
    case PHASE_PASSING:
        draw_left_panel_chat(rs);
        draw_left_panel_info(rs);
        draw_phase_passing(gs, rs);
        break;
    case PHASE_PLAYING:
        draw_phase_playing(gs, rs);
        particle_draw(&rs->particles);
        break;
    case PHASE_SCORING:
        draw_left_panel_chat(rs);
        draw_left_panel_info(rs);
        draw_phase_scoring(gs, rs);
        particle_draw(&rs->particles);
        break;
    default:
        break;
    }
}

/* ---- Main draw ---- */

void render_draw(const GameState *gs, const RenderState *rs)
{
    BeginDrawing();
    ClearBackground((Color){20, 60, 20, 255});

    switch (gs->phase) {
    case PHASE_LOGIN:
        draw_phase_login(gs, rs);
        break;

    case PHASE_ONLINE_MENU:
        draw_phase_online(gs, rs);
        break;

    case PHASE_MENU:
        draw_phase_menu(gs, rs);
        break;

    case PHASE_STATS:
        draw_phase_stats(gs, rs);
        break;

    case PHASE_DEALING:
    case PHASE_PASSING:
    case PHASE_PLAYING:
    case PHASE_SCORING:
        draw_ingame_phase(gs, rs, gs->phase);
        break;

    case PHASE_GAME_OVER:
        draw_phase_game_over(gs, rs);
        break;

    case PHASE_SETTINGS:
        /* If opened from an in-game pause, show the game underneath (dimmed) */
        if (is_ingame_phase(rs->settings_return_phase)) {
            draw_ingame_phase(gs, rs, rs->settings_return_phase);
            DrawRectangle(0, 0, (int)rs->layout.screen_width,
                          (int)rs->layout.screen_height,
                          (Color){0, 0, 0, 180});
        }
        draw_phase_settings(gs, rs);
        break;

    default:
        break;
    }

    /* Pause overlay (drawn on top of the game scene, but not during settings) */
    if (rs->pause_state != PAUSE_INACTIVE && gs->phase != PHASE_SETTINGS) {
        draw_pause_overlay(rs);
    }

    DrawFPS(10, 10);
    EndDrawing();
}

/* ---- Hit testing ---- */

int render_hit_test_card(const RenderState *rs, Vector2 mouse_pos)
{
    float s = rs->layout.scale;
    for (int i = rs->hand_visual_counts[HUMAN_PLAYER] - 1; i >= 0; i--) {
        int idx = rs->hand_visuals[HUMAN_PLAYER][i];
        const CardVisual *cv = &rs->cards[idx];

        Vector2 pos = cv->position;
        if (cv->selected) {
            pos.y -= CARD_SELECT_LIFT_REF * s;
        }

        float w = CARD_WIDTH_REF * cv->scale;
        float h = CARD_HEIGHT_REF * cv->scale;

        /* Transform mouse into card-local (unrotated) space */
        float angle_rad = cv->rotation * DEG2RAD;
        float cos_a = cosf(-angle_rad);
        float sin_a = sinf(-angle_rad);
        float dx = mouse_pos.x - pos.x;
        float dy = mouse_pos.y - pos.y;
        float local_x = dx * cos_a - dy * sin_a;
        float local_y = dx * sin_a + dy * cos_a;

        Rectangle card_rect = {-cv->origin.x, -cv->origin.y, w, h};
        if (CheckCollisionPointRec((Vector2){local_x, local_y}, card_rect)) {
            return idx;
        }
    }
    return -1;
}

int render_hit_test_opponent_card(const RenderState *rs, Vector2 mouse_pos,
                                   int *out_player)
{
    *out_player = -1;
    for (int p = 1; p < NUM_PLAYERS; p++) {
        for (int i = rs->hand_visual_counts[p] - 1; i >= 0; i--) {
            int idx = rs->hand_visuals[p][i];
            if (idx < 0 || idx >= rs->card_count) continue;
            const CardVisual *cv = &rs->cards[idx];

            float w = CARD_WIDTH_REF * cv->scale;
            float h = CARD_HEIGHT_REF * cv->scale;

            /* Transform mouse into card-local (unrotated) space */
            float angle_rad = cv->rotation * DEG2RAD;
            float cos_a = cosf(-angle_rad);
            float sin_a = sinf(-angle_rad);
            float dx = mouse_pos.x - cv->position.x;
            float dy = mouse_pos.y - cv->position.y;
            float local_x = dx * cos_a - dy * sin_a;
            float local_y = dx * sin_a + dy * cos_a;

            Rectangle card_rect = {-cv->origin.x, -cv->origin.y, w, h};
            if (CheckCollisionPointRec((Vector2){local_x, local_y}, card_rect)) {
                *out_player = p;
                return idx;
            }
        }
    }
    return -1;
}

bool render_hit_test_button(const UIButton *btn, Vector2 mouse_pos)
{
    if (!btn->visible || btn->disabled) return false;
    return CheckCollisionPointRec(mouse_pos, btn->bounds);
}

int render_toggle_card_selection(RenderState *rs, int card_visual_index)
{
    for (int i = 0; i < rs->selected_count; i++) {
        if (rs->selected_indices[i] == card_visual_index) {
            rs->cards[card_visual_index].selected = false;
            for (int j = i; j < rs->selected_count - 1; j++) {
                rs->selected_indices[j] = rs->selected_indices[j + 1];
            }
            rs->selected_count--;
            rs->selected_indices[rs->selected_count] = -1;
            return rs->selected_count;
        }
    }

    if (rs->selected_count < rs->pass_card_limit) {
        rs->selected_indices[rs->selected_count++] = card_visual_index;
        rs->cards[card_visual_index].selected = true;
    }

    return rs->selected_count;
}

void render_clear_selection(RenderState *rs)
{
    for (int i = 0; i < rs->selected_count; i++) {
        int idx = rs->selected_indices[i];
        if (idx >= 0 && idx < rs->card_count) {
            rs->cards[idx].selected = false;
        }
        rs->selected_indices[i] = -1;
    }
    rs->selected_count = 0;
}

void render_clear_piles(RenderState *rs)
{
    rs->pile_card_count = 0;
    rs->pile_anim_in_progress = false;
}

void render_cancel_drag(RenderState *rs)
{
    rs->drag.active = false;
    rs->drag.snap_back = false;
    rs->drag.card_visual_idx = -1;
    rs->drag.has_release_pos = false;
    rs->drag.hand_slot_origin = -1;
    rs->drag.hand_slot_current = -1;
    rs->drag.is_play_drag = false;
    rs->drag.rearrange_count = 0;
}

void render_start_card_drag(RenderState *rs, int cv_idx, int hand_slot,
                             Vector2 mouse, bool is_play_drag)
{
    CardVisual *cv = &rs->cards[cv_idx];
    rs->drag.active = true;
    rs->drag.card_visual_idx = cv_idx;
    rs->drag.grab_offset = (Vector2){
        mouse.x - (cv->position.x - cv->origin.x),
        mouse.y - (cv->position.y - cv->origin.y),
    };
    rs->drag.current_pos = cv->position;
    rs->drag.has_release_pos = false;
    rs->drag.original_pos = cv->position;
    rs->drag.original_rot = cv->rotation;
    rs->drag.original_z = cv->z_order;
    rs->drag.prev_pos = cv->position;
    rs->drag.velocity = (Vector2){0, 0};
    rs->drag.hand_slot_origin = hand_slot;
    rs->drag.hand_slot_current = hand_slot;
    rs->drag.is_play_drag = is_play_drag;
    cv->z_order = 200;

    /* Build initial rearrange_map */
    rs->drag.rearrange_count = 0;
    for (int i = 0; i < rs->hand_visual_counts[HUMAN_PLAYER]; i++) {
        if (i == hand_slot) continue;
        rs->drag.rearrange_map[rs->drag.rearrange_count++] = i;
    }
}

void render_commit_hand_reorder(GameState *gs, RenderState *rs,
                                 Phase2State *p2)
{
    int src = rs->drag.hand_slot_origin;
    int dst = rs->drag.hand_slot_current;
    int count = gs->players[0].hand.count;
    if (src != dst && src >= 0 && dst >= 0 && src < count && dst < count) {
        hand_move_card(&gs->players[0].hand, src, dst);

        /* Shift render transmute IDs */
        int saved_id = rs->hand_transmute_ids[src];
        if (src < dst) {
            for (int i = src; i < dst; i++)
                rs->hand_transmute_ids[i] = rs->hand_transmute_ids[i + 1];
        } else {
            for (int i = src; i > dst; i--)
                rs->hand_transmute_ids[i] = rs->hand_transmute_ids[i - 1];
        }
        rs->hand_transmute_ids[dst] = saved_id;

        /* Shift authoritative phase2 hand transmute slots to stay in sync */
        if (p2 && p2->enabled) {
            TransmuteSlot *slots = p2->players[0].hand_transmutes.slots;
            TransmuteSlot saved_slot = slots[src];
            if (src < dst) {
                for (int i = src; i < dst; i++)
                    slots[i] = slots[i + 1];
            } else {
                for (int i = src; i > dst; i--)
                    slots[i] = slots[i - 1];
            }
            slots[dst] = saved_slot;
        }

        rs->sync_needed = true;
    }
}

void render_update_snap_target(RenderState *rs)
{
    int slot = rs->drag.hand_slot_current;
    int count = rs->hand_visual_counts[HUMAN_PLAYER];
    Vector2 positions[MAX_HAND_SIZE];
    float rotations[MAX_HAND_SIZE];
    int out_count = 0;
    layout_hand_positions(POS_BOTTOM, count, &rs->layout,
                          positions, rotations, &out_count);
    if (slot >= 0 && slot < out_count) {
        rs->drag.original_pos = positions[slot];
        rs->drag.original_rot = rotations[slot];
    }
}

int render_hit_test_contract(const RenderState *rs, Vector2 mouse_pos)
{
    if (!rs->contract_ui_active) return -1;
    for (int i = 0; i < rs->contract_option_count; i++) {
        if (render_hit_test_button(&rs->contract_options[i], mouse_pos)) {
            return i;
        }
    }
    return -1;
}

int render_hit_test_transmute(const RenderState *rs, Vector2 mouse_pos)
{
    for (int i = 0; i < rs->transmute_btn_count; i++) {
        if (rs->transmute_btns[i].visible &&
            !rs->transmute_btns[i].disabled &&
            CheckCollisionPointRec(mouse_pos, rs->transmute_btns[i].bounds)) {
            return i;
        }
    }
    return -1;
}

void render_set_contract_options(RenderState *rs, const int ids[], int count,
                                 const char *names[], const char *descs[])
{
    if (count > 4) count = 4;
    rs->contract_option_count = count;
    rs->selected_contract_idx = -1;
    rs->contract_ui_active = (count > 0);

    Rectangle rects[4];
    layout_contract_options(&rs->layout, count, rects);

    for (int i = 0; i < count; i++) {
        rs->contract_options[i].bounds = rects[i];
        rs->contract_options[i].label = names[i];
        rs->contract_options[i].subtitle = descs[i];
        rs->contract_options[i].visible = true;
        rs->contract_options[i].disabled = false;
        rs->contract_options[i].hovered = false;
        rs->contract_options[i].pressed = false;
        rs->contract_option_ids[i] = ids[i];
    }
}

void render_chat_log_push_color(RenderState *rs, const char *msg, Color color)
{
    int slot;
    if (rs->chat_count < CHAT_LOG_MAX) {
        slot = (rs->chat_head + rs->chat_count) % CHAT_LOG_MAX;
        rs->chat_count++;
    } else {
        slot = rs->chat_head;
        rs->chat_head = (rs->chat_head + 1) % CHAT_LOG_MAX;
    }
    snprintf(rs->chat_msgs[slot], CHAT_MSG_LEN, "%s", msg);
    rs->chat_colors[slot] = color;
}

void render_chat_log_push(RenderState *rs, const char *msg)
{
    render_chat_log_push_color(rs, msg, LIGHTGRAY);
}

