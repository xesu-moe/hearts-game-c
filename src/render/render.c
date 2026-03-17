/* ============================================================
 * @deps-implements: render.h
 * @deps-requires: render.h (RenderState), particle.h
 *                 (particle_init, particle_update, particle_draw),
 *                 anim.h, layout.h, card_render.h,
 *                 core/game_state.h (GamePhase, PassSubphase),
 *                 core/card.h (NUM_PLAYERS),
 *                 core/settings.h (GameSettings),
 *                 phase2/effect.h (ActiveEffect),
 *                 raylib.h, rlgl.h, math.h (ceilf)
 * @deps-last-changed: 2026-03-17 — Added particle_init, particle_update, particle_draw calls
 * ============================================================ */

#include "render.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "card_render.h"
#include "rlgl.h"

/* ---- Internal constants ---- */

#define ANIM_PLAY_CARD_DURATION  0.25f
#define ANIM_PASS_CARD_DURATION  0.4f
#define ANIM_TRICK_COLLECT_DUR   0.3f
#define TRICK_WINNER_DISPLAY_TIME 1.0f

#define HUMAN_PLAYER 0

/* Map player_id to screen position: 0=bottom, 1=left, 2=top, 3=right */
static PlayerPosition player_screen_pos(int player_id)
{
    static const PlayerPosition positions[NUM_PLAYERS] = {
        POS_BOTTOM, POS_LEFT, POS_TOP, POS_RIGHT
    };
    return positions[player_id];
}

/* ---- Card visual helpers ---- */

static int alloc_card_visual(RenderState *rs)
{
    if (rs->card_count >= MAX_CARD_VISUALS) return -1;
    int idx = rs->card_count++;
    memset(&rs->cards[idx], 0, sizeof(CardVisual));
    rs->cards[idx].scale = 1.0f;
    rs->cards[idx].opacity = 1.0f;
    return idx;
}

static void start_animation(CardVisual *cv, Vector2 target, float target_rot,
                            float duration, EaseType ease)
{
    cv->start = cv->position;
    cv->start_rotation = cv->rotation;
    cv->target = target;
    cv->target_rotation = target_rot;
    cv->anim_elapsed = 0.0f;
    cv->anim_duration = duration;
    cv->anim_ease = ease;
    cv->animating = true;
    cv->anim_delay = 0.0f;
}

static void update_animation(CardVisual *cv, float dt)
{
    if (!cv->animating) return;

    if (cv->anim_delay > 0.0f) {
        cv->anim_delay -= dt;
        if (cv->anim_delay > 0.0f) return;
        dt = -cv->anim_delay; /* overflow into elapsed */
        cv->anim_delay = 0.0f;
    }

    cv->anim_elapsed += dt;
    if (cv->anim_duration <= 0.0f) {
        cv->position = cv->target;
        cv->rotation = cv->target_rotation;
        cv->animating = false;
        return;
    }
    float t = cv->anim_elapsed / cv->anim_duration;
    if (t >= 1.0f) {
        t = 1.0f;
        cv->animating = false;
    }

    float eased = ease_apply(cv->anim_ease, t);
    cv->position.x = lerpf(cv->start.x, cv->target.x, eased);
    cv->position.y = lerpf(cv->start.y, cv->target.y, eased);
    cv->rotation = lerpf(cv->start_rotation, cv->target_rotation, eased);
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

        /* Compute rotation origin based on player position */
        Vector2 origin;
        switch (spos) {
        case POS_BOTTOM: origin = (Vector2){cw_s * 0.5f, ch_s};       break;
        case POS_TOP:    origin = (Vector2){cw_s * 0.5f, 0.0f};       break;
        case POS_LEFT:   origin = (Vector2){0.0f,        ch_s * 0.5f}; break;
        case POS_RIGHT:  origin = (Vector2){cw_s,        ch_s * 0.5f}; break;
        default:         origin = (Vector2){0.0f, 0.0f};              break;
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
        }
    }

    /* Sync trick cards (skip during scoring/game-over — the last trick
     * has already been collected but current_trick still holds stale data) */
    const Trick *trick = &gs->current_trick;
    if (gs->phase != PHASE_PLAYING) {
        trick = NULL;
    }
    for (int i = 0; trick != NULL && i < trick->num_played; i++) {
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
    }
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
            [MENU_PLAY_ONLINE]   = {"Play Online",   "(Coming Soon)", true},
            [MENU_PLAY_OFFLINE]  = {"Play Offline",   NULL,            false},
            [MENU_DECK_BUILDING] = {"Deck Building",  "(Coming Soon)", true},
            [MENU_STATISTICS]    = {"Statistics",      "(Coming Soon)", true},
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

    rs->btn_confirm_pass.bounds = btn_rect;
    rs->btn_confirm_pass.label = "Confirm Pass";
    if (rs->contract_ui_active) {
        rs->btn_confirm_pass.visible =
            (gs->phase == PHASE_PASSING &&
             rs->selected_count == PASS_CARD_COUNT &&
             rs->selected_contract_idx >= 0);
    } else {
        rs->btn_confirm_pass.visible =
            (gs->phase == PHASE_PASSING && rs->selected_count == PASS_CARD_COUNT);
    }

    if (gs->phase == PHASE_GAME_OVER) {
        rs->btn_continue.bounds = (Rectangle){
            bc.x - 100.0f * s,
            cfg->board_y + 460.0f * s,
            200.0f * s, 50.0f * s
        };
        rs->btn_continue.label = "Return to Menu";
        rs->btn_continue.visible = true;
    } else {
        rs->btn_continue.bounds = btn_rect;
        rs->btn_continue.label = "Continue";
        rs->btn_continue.visible = (gs->phase == PHASE_SCORING);
    }

    /* ---- Info panel revenge buttons ---- */
    {
        Rectangle lp = layout_left_panel_lower(cfg);
        float btn_w = lp.width - 16.0f * s;
        float btn_h = 32.0f * s;
        float gap2 = 5.0f * s;
        int cnt = rs->info_revenge_count;
        /* Stack buttons from bottom of info panel upwards */
        float bot = lp.y + lp.height - 8.0f * s;
        rs->info_revenge_skip_btn.bounds = (Rectangle){
            lp.x + 8.0f * s, bot - btn_h, btn_w, btn_h};
        bot -= btn_h + gap2;
        for (int i = cnt - 1; i >= 0; i--) {
            rs->info_revenge_btns[i].bounds = (Rectangle){
                lp.x + 8.0f * s, bot - btn_h, btn_w, btn_h};
        }
    }

    /* ---- Grudge discard buttons ---- */
    if (rs->grudge_discard_ui) {
        float dbw = 200.0f * s, dbh = 45.0f * s, dgap = 12.0f * s;
        rs->btn_keep_old_grudge.bounds = (Rectangle){
            bc.x - dbw - dgap * 0.5f, bc.y, dbw, dbh};
        rs->btn_keep_old_grudge.label = "Keep Old";
        rs->btn_keep_old_grudge.visible = true;

        rs->btn_keep_new_grudge.bounds = (Rectangle){
            bc.x + dgap * 0.5f, bc.y, dbw, dbh};
        rs->btn_keep_new_grudge.label = "Keep New";
        rs->btn_keep_new_grudge.visible = true;
    } else {
        rs->btn_keep_old_grudge.visible = false;
        rs->btn_keep_new_grudge.visible = false;
    }

    /* ---- Settings screen buttons ---- */
    if (gs->phase == PHASE_SETTINGS) {
        float row_w = 500.0f * s;
        float row_h = 40.0f * s;
        float row_gap = 8.0f * s;
        float arrow_sz = 30.0f * s;
        float scx = cfg->screen_width * 0.5f;
        float scy = cfg->screen_height * 0.5f;
        float start_y = scy - 180.0f * s;
        float cx = scx;

        static const char *labels[] = {
            "Window Mode", "Resolution", "FPS Cap",
            "Anim Speed", "AI Speed",
            "Master Volume", "Music Volume", "SFX Volume"
        };

        for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
            float y = start_y + (float)i * (row_h + row_gap);
            /* Add section gaps */
            if (i >= 3) y += 16.0f * s; /* gap before Gameplay */
            if (i >= 5) y += 16.0f * s; /* gap before Audio */

            rs->settings_labels[i] = labels[i];
            rs->settings_disabled[i] = (i >= SETTINGS_ACTIVE_COUNT);

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

        /* Apply display settings button (between display section and back) */
        float apply_y = start_y + 3.0f * (row_h + row_gap) - 6.0f * s;
        rs->btn_settings_apply.bounds = (Rectangle){
            cx + row_w * 0.5f - 100.0f * s, apply_y, 100.0f * s, 30.0f * s};
        rs->btn_settings_apply.label = "Apply";
        rs->btn_settings_apply.visible = true;
        rs->btn_settings_apply.disabled = false;
        rs->btn_settings_apply.subtitle = NULL;

        /* Back button */
        rs->btn_settings_back.bounds = (Rectangle){
            scx - 80.0f * s,
            start_y + (float)SETTINGS_ROW_COUNT * (row_h + row_gap) + 60.0f * s,
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
    }
}

/* ---- Public API ---- */

void render_init(RenderState *rs)
{
    memset(rs, 0, sizeof(*rs));
    rs->hover_card_index = -1;
    rs->last_trick_winner = -1;
    rs->current_phase = PHASE_MENU;
    rs->layout_dirty = true;
    rs->sync_needed = true;
    rs->anim_play_player = -1;

    /* Initialize mutable layout with defaults */
    layout_recalculate(&rs->layout, 1280, 720);

    for (int i = 0; i < PASS_CARD_COUNT; i++) {
        rs->selected_indices[i] = -1;
    }

    rs->contract_option_count = 0;
    rs->selected_contract_idx = -1;
    rs->contract_ui_active = false;
    rs->show_contract_results = false;

    particle_init(&rs->particles);
}

void render_update(const GameState *gs, RenderState *rs, float dt)
{
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

    /* Sync visuals only when game state has mutated or phase changed */
    if (rs->phase_just_changed || rs->sync_needed) {
        /* Save selected card identities before resync */
        Card saved_selected[PASS_CARD_COUNT];
        int saved_count = 0;
        for (int i = 0; i < rs->selected_count; i++) {
            int idx = rs->selected_indices[i];
            if (idx >= 0 && idx < rs->card_count) {
                saved_selected[saved_count++] = rs->cards[idx].card;
            }
        }

        int anim_player = rs->anim_play_player;
        rs->anim_play_player = -1;

        sync_hands(gs, rs);

        /* Restore selections by matching card identity */
        rs->selected_count = 0;
        if (gs->phase == PHASE_PASSING && saved_count > 0) {
            for (int si = 0; si < saved_count; si++) {
                for (int i = 0; i < rs->hand_visual_counts[HUMAN_PLAYER]; i++) {
                    int idx = rs->hand_visuals[HUMAN_PLAYER][i];
                    if (card_equals(rs->cards[idx].card, saved_selected[si])) {
                        rs->selected_indices[rs->selected_count++] = idx;
                        rs->cards[idx].selected = true;
                        break;
                    }
                }
            }
        }

        /* Animate the last trick card from hand center to trick position */
        if (anim_player >= 0 && rs->trick_visual_count > 0) {
            int trick_idx = rs->trick_visuals[rs->trick_visual_count - 1];
            CardVisual *cv = &rs->cards[trick_idx];

            PlayerPosition spos = player_screen_pos(anim_player);
            Vector2 hand_center = layout_trick_position(spos, &rs->layout);
            float start_rot = 0.0f;
            if (rs->hand_visual_counts[anim_player] > 0) {
                int mid = rs->hand_visual_counts[anim_player] / 2;
                int mid_idx = rs->hand_visuals[anim_player][mid];
                const CardVisual *mid_cv = &rs->cards[mid_idx];
                hand_center = (Vector2){
                    mid_cv->position.x - mid_cv->origin.x,
                    mid_cv->position.y - mid_cv->origin.y
                };
                start_rot = mid_cv->rotation;
            }

            Vector2 target = cv->position;
            cv->position = hand_center;
            cv->rotation = start_rot;
            start_animation(cv, target, 0.0f, ANIM_PLAY_CARD_DURATION,
                            EASE_OUT_QUAD);
        }

        rs->sync_needed = false;
    }

    /* Update cached display values */
    for (int i = 0; i < NUM_PLAYERS; i++) {
        rs->displayed_round_points[i] = gs->players[i].round_points;
        rs->displayed_total_scores[i] = gs->players[i].total_score;
    }
    rs->displayed_pass_dir = gs->pass_direction;

    /* Trick winner display timer */
    if (rs->trick_winner_timer > 0.0f) {
        rs->trick_winner_timer -= dt;
    }

    /* Update animations */
    for (int i = 0; i < rs->card_count; i++) {
        update_animation(&rs->cards[i], dt);
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

    /* Update grudge/discard/info panel button hover state */
    if (gs->phase == PHASE_PLAYING) {
        Vector2 mouse = GetMousePosition();
        rs->btn_keep_old_grudge.hovered =
            rs->btn_keep_old_grudge.visible &&
            CheckCollisionPointRec(mouse, rs->btn_keep_old_grudge.bounds);
        rs->btn_keep_new_grudge.hovered =
            rs->btn_keep_new_grudge.visible &&
            CheckCollisionPointRec(mouse, rs->btn_keep_new_grudge.bounds);
        /* Info panel revenge buttons */
        if (rs->info_grudge_available && rs->info_grudge_interactive) {
            for (int i = 0; i < rs->info_revenge_count; i++) {
                rs->info_revenge_btns[i].hovered =
                    rs->info_revenge_btns[i].visible &&
                    CheckCollisionPointRec(mouse,
                                           rs->info_revenge_btns[i].bounds);
            }
            rs->info_revenge_skip_btn.hovered =
                rs->info_revenge_skip_btn.visible &&
                CheckCollisionPointRec(mouse,
                                       rs->info_revenge_skip_btn.bounds);
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

static void draw_card_visual(const CardVisual *cv, float ui_scale)
{
    Vector2 pos = cv->position;

    /* Lift selected cards (upward for bottom player) */
    if (cv->selected) {
        pos.y -= CARD_SELECT_LIFT_REF * ui_scale;
    }

    if (cv->face_up) {
        card_render_face(cv->card, pos, cv->scale, cv->opacity,
                         cv->hovered, cv->selected,
                         cv->rotation, cv->origin);
    } else {
        card_render_back(pos, cv->scale, cv->opacity,
                         cv->rotation, cv->origin);
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

        DrawRectangleRounded(btn->bounds, 0.2f, 4, bg);
        DrawRectangleRoundedLines(btn->bounds, 0.2f, 4, border_col);

        int font_size = (int)(18.0f * s);
        int tw2 = MeasureText(btn->label, font_size);

        if (btn->subtitle != NULL) {
            int sub_size = (int)(12.0f * s);
            int sw = MeasureText(btn->subtitle, sub_size);
            int total_h = font_size + (int)(3.0f * s) + sub_size;
            int y_start = (int)(btn->bounds.y +
                                (btn->bounds.height - (float)total_h) * 0.5f);

            DrawText(btn->label,
                     (int)(btn->bounds.x + (btn->bounds.width - (float)tw2) * 0.5f),
                     y_start, font_size, text_col);

            Color sub_col = (i == rs->selected_contract_idx)
                                ? (Color){255, 255, 200, 255}
                                : LIGHTGRAY;
            DrawText(btn->subtitle,
                     (int)(btn->bounds.x + (btn->bounds.width - (float)sw) * 0.5f),
                     y_start + font_size + (int)(3.0f * s), sub_size, sub_col);
        } else {
            float text_y = btn->bounds.y +
                           (btn->bounds.height - (float)font_size) * 0.5f;
            DrawText(btn->label,
                     (int)(btn->bounds.x + (btn->bounds.width - (float)tw2) * 0.5f),
                     (int)text_y, font_size, text_col);
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

    /* Top-right of board area */
    float bx = rs->layout.board_x + rs->layout.board_size - 60.0f * s;
    float by = rs->layout.board_y + 10.0f * s;
    DrawText(timer_text, (int)bx, (int)by, timer_size, timer_col);
}

static void draw_phase_passing(const GameState *gs, const RenderState *rs)
{
    float s = rs->layout.scale;
    Vector2 bc = layout_board_center(&rs->layout);

    /* Subphase-specific content */
    switch (rs->pass_subphase) {
    case PASS_SUB_HOST_ACTION:
        /* Status text centered on board */
        if (rs->pass_status_text) {
            int st_size = (int)(22.0f * s);
            int st_w = MeasureText(rs->pass_status_text, st_size);
            float label_y = (rs->contract_option_count > 0)
                                ? rs->contract_options[0].bounds.y - 32.0f * s
                                : bc.y - 140.0f * s;
            DrawText(rs->pass_status_text,
                     (int)(bc.x - (float)st_w * 0.5f),
                     (int)label_y, st_size, LIGHTGRAY);
        }
        /* Reuse contract option buttons for host action choices */
        if (rs->contract_ui_active) {
            draw_contract_buttons(rs, s);
        }
        draw_subphase_timer(rs, s);
        break;

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
                 rs->selected_count, PASS_CARD_COUNT);
        int sel_size = (int)(20.0f * s);
        int sel_w = MeasureText(sel_text, sel_size);
        DrawText(sel_text, (int)(bc.x - (float)sel_w * 0.5f),
                 (int)(rs->layout.board_y + 270.0f * s), sel_size, LIGHTGRAY);

        draw_subphase_timer(rs, s);
        draw_button(&rs->btn_confirm_pass, s);
        break;
    }
    }

    /* Draw all hands (all subphases) */
    for (int i = 0; i < rs->card_count; i++) {
        draw_card_visual(&rs->cards[i], s);
    }
}

static void draw_grudge_tokens(const RenderState *rs)
{
    float s = rs->layout.scale;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (!rs->player_has_grudge[i]) continue;
        PlayerPosition spos = player_screen_pos(i);
        Vector2 pos = layout_grudge_token_position(spos, &rs->layout);
        DrawCircle((int)(pos.x + 8.0f * s), (int)(pos.y + 8.0f * s),
                   10.0f * s, MAROON);
        DrawText("G", (int)(pos.x + 3.0f * s), (int)(pos.y + 1.0f * s),
                 (int)(16.0f * s), WHITE);
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
    float line_h = 18.0f * s;
    float text_x = r.x + 6.0f * s;
    float max_w = r.width - 12.0f * s;

    /* How many messages fit? */
    float content_top = r.y + 4.0f * s;
    float content_bot = r.y + r.height - 4.0f * s;
    int max_visible = (int)((content_bot - content_top) / line_h);
    if (max_visible < 1) max_visible = 1;

    /* Draw newest messages at bottom */
    int count = rs->chat_count;
    int start = (count > max_visible) ? count - max_visible : 0;
    for (int i = start; i < count; i++) {
        int ring_idx = (rs->chat_head + i) % CHAT_LOG_MAX;
        float y = content_bot - (float)(count - i) * line_h;
        if (y < content_top) continue;
        /* Truncate long messages to panel width */
        char buf[CHAT_MSG_LEN];
        snprintf(buf, sizeof(buf), "%s", rs->chat_msgs[ring_idx]);
        while (MeasureText(buf, msg_fs) > (int)max_w && buf[0] != '\0') {
            buf[strlen(buf) - 1] = '\0';
        }
        DrawText(buf, (int)text_x, (int)y, msg_fs, LIGHTGRAY);
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
    DrawText("Contract", (int)x, (int)y, header_fs, GOLD);
    y += (float)header_fs + 4.0f * s;
    if (rs->info_contract_active) {
        DrawText(rs->info_contract_name, (int)x, (int)y, body_fs, WHITE);
        y += (float)body_fs + 2.0f * s;
        /* Word-wrap description crudely */
        char desc[128];
        snprintf(desc, sizeof(desc), "%s", rs->info_contract_desc);
        while (MeasureText(desc, body_fs) > (int)max_w && desc[0] != '\0') {
            desc[strlen(desc) - 1] = '\0';
        }
        DrawText(desc, (int)x, (int)y, body_fs, LIGHTGRAY);
        y += (float)body_fs + 2.0f * s;
    } else {
        DrawText("None", (int)x, (int)y, body_fs, GRAY);
        y += (float)body_fs + 2.0f * s;
    }

    y += 6.0f * s;

    /* Host Action section */
    DrawText("Host Action", (int)x, (int)y, header_fs, GOLD);
    y += (float)header_fs + 4.0f * s;
    if (rs->info_host_active) {
        DrawText(rs->info_host_name, (int)x, (int)y, body_fs, WHITE);
        y += (float)body_fs + 2.0f * s;
        char desc[128];
        snprintf(desc, sizeof(desc), "%s", rs->info_host_desc);
        while (MeasureText(desc, body_fs) > (int)max_w && desc[0] != '\0') {
            desc[strlen(desc) - 1] = '\0';
        }
        DrawText(desc, (int)x, (int)y, body_fs, LIGHTGRAY);
        y += (float)body_fs + 2.0f * s;
    } else {
        DrawText("None", (int)x, (int)y, body_fs, GRAY);
        y += (float)body_fs + 2.0f * s;
    }

    y += 6.0f * s;

    /* Bonuses section */
    DrawText("Bonuses", (int)x, (int)y, header_fs, GOLD);
    y += (float)header_fs + 4.0f * s;
    if (rs->info_bonus_count > 0) {
        for (int i = 0; i < rs->info_bonus_count; i++) {
            DrawText(rs->info_bonus_text[i], (int)x, (int)y, body_fs, GREEN);
            y += (float)body_fs + 2.0f * s;
        }
    } else {
        DrawText("None", (int)x, (int)y, body_fs, GRAY);
        y += (float)body_fs + 2.0f * s;
    }

    y += 6.0f * s;

    /* Grudge section */
    if (rs->info_grudge_available) {
        char grudge_hdr[32];
        snprintf(grudge_hdr, sizeof(grudge_hdr), "Grudge vs %s",
                 rs->info_grudge_attacker_name);
        DrawText(grudge_hdr, (int)x, (int)y, header_fs, MAROON);
        y += (float)header_fs + 4.0f * s;

        for (int i = 0; i < rs->info_revenge_count; i++) {
            draw_button(&rs->info_revenge_btns[i], s);
        }
        draw_button(&rs->info_revenge_skip_btn, s);
    }

    EndScissorMode();
}

static void draw_grudge_discard_ui(const RenderState *rs)
{
    float s = rs->layout.scale;

    Rectangle br = layout_board_rect(&rs->layout);
    DrawRectangleRec(br, (Color){0, 0, 0, 150});

    Vector2 bc = layout_board_center(&rs->layout);

    const char *title = "New Grudge Token!";
    int title_fs = (int)(28.0f * s);
    int tw = MeasureText(title, title_fs);
    DrawText(title, (int)(bc.x - (float)tw * 0.5f),
             (int)(bc.y - 80.0f * s), title_fs, GOLD);

    const char *sub = "You already hold a grudge. Choose which to keep:";
    int sub_fs = (int)(18.0f * s);
    int sw = MeasureText(sub, sub_fs);
    DrawText(sub, (int)(bc.x - (float)sw * 0.5f),
             (int)(bc.y - 45.0f * s), sub_fs, LIGHTGRAY);

    draw_button(&rs->btn_keep_old_grudge, s);
    draw_button(&rs->btn_keep_new_grudge, s);
}

static void draw_phase_playing(const GameState *gs, const RenderState *rs)
{
    float s = rs->layout.scale;
    const LayoutConfig *cfg = &rs->layout;

    /* Left column panels */
    draw_left_panel_chat(rs);
    draw_left_panel_info(rs);

    /* Draw hands */
    for (int i = 0; i < rs->card_count; i++) {
        draw_card_visual(&rs->cards[i], s);
    }

    /* Grudge token icons */
    draw_grudge_tokens(rs);

    /* Current player indicator */
    int current = game_state_current_player(gs);
    if (current >= 0) {
        PlayerPosition spos = player_screen_pos(current);
        Vector2 name_pos = layout_name_position(spos, cfg);
        const char *name = player_name(current);
        Color col = (current == HUMAN_PLAYER) ? GREEN : YELLOW;
        DrawText(name, (int)name_pos.x, (int)name_pos.y, (int)(18.0f * s), col);
    }

    /* Trick count */
    char trick_text[32];
    snprintf(trick_text, sizeof(trick_text), "Trick %d/13", gs->tricks_played + 1);
    DrawText(trick_text,
             (int)(cfg->board_x + cfg->board_size - 120.0f * s),
             (int)(cfg->board_y + 30.0f * s), (int)(18.0f * s), LIGHTGRAY);

    /* Dim unplayable cards for human player during their turn */
    if (current == HUMAN_PLAYER) {
        for (int i = 0; i < rs->hand_visual_counts[HUMAN_PLAYER]; i++) {
            int idx = rs->hand_visuals[HUMAN_PLAYER][i];
            const CardVisual *cv = &rs->cards[idx];
            if (!game_state_is_valid_play(gs, HUMAN_PLAYER, cv->card)) {
                Vector2 pos = cv->position;
                float w = CARD_WIDTH_REF * cv->scale;
                float h = CARD_HEIGHT_REF * cv->scale;

                rlPushMatrix();
                rlTranslatef(pos.x, pos.y, 0.0f);
                rlRotatef(cv->rotation, 0.0f, 0.0f, 1.0f);
                rlTranslatef(-cv->origin.x, -cv->origin.y, 0.0f);

                Rectangle dim_rect = {0, 0, w, h};
                DrawRectangleRounded(dim_rect, 0.15f, 4,
                                     (Color){0, 0, 0, 100});

                rlPopMatrix();
            }
        }
    }
}

static void draw_phase_scoring(const GameState *gs, const RenderState *rs)
{
    float s = rs->layout.scale;
    const LayoutConfig *cfg = &rs->layout;

    Rectangle br = layout_board_rect(cfg);
    DrawRectangleRec(br, (Color){0, 0, 0, 150});

    Vector2 bc = layout_board_center(cfg);

    const char *title = "Round Complete";
    int title_size = (int)(36.0f * s);
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(bc.x - (float)tw * 0.5f),
             (int)(cfg->board_y + 100.0f * s), title_size, RAYWHITE);

    /* Score table */
    int col_w = (int)(150.0f * s);
    int table_x = (int)(cfg->board_x + (cfg->board_size - (float)(col_w * 3)) * 0.5f);
    int table_y = (int)(cfg->board_y + 180.0f * s);
    int row_h = (int)(40.0f * s);
    int font22 = (int)(22.0f * s);

    /* Header */
    DrawText("Player", table_x, table_y, font22, LIGHTGRAY);
    DrawText("Round", table_x + col_w, table_y, font22, LIGHTGRAY);
    DrawText("Total", table_x + col_w * 2, table_y, font22, LIGHTGRAY);

    DrawLine(table_x, table_y + (int)(28.0f * s),
             table_x + col_w * 3, table_y + (int)(28.0f * s), GRAY);

    /* Find best/worst for highlighting */
    int best_score = 999, worst_score = -1;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (gs->players[i].total_score < best_score)
            best_score = gs->players[i].total_score;
        if (gs->players[i].total_score > worst_score)
            worst_score = gs->players[i].total_score;
    }

    for (int i = 0; i < NUM_PLAYERS; i++) {
        int y = table_y + row_h * (i + 1);
        Color row_col = RAYWHITE;
        if (gs->players[i].total_score == best_score) row_col = GREEN;
        if (gs->players[i].total_score == worst_score) row_col = RED;

        DrawText(player_name(i), table_x, y, font22, row_col);

        char pts[16];
        snprintf(pts, sizeof(pts), "%d", rs->displayed_round_points[i]);
        DrawText(pts, table_x + col_w, y, font22, row_col);

        snprintf(pts, sizeof(pts), "%d", rs->displayed_total_scores[i]);
        DrawText(pts, table_x + col_w * 2, y, font22, row_col);
    }

    /* Contract results */
    if (rs->show_contract_results) {
        int cr_y = table_y + row_h * (NUM_PLAYERS + 1) + (int)(20.0f * s);
        DrawLine(table_x, cr_y - (int)(8.0f * s),
                 table_x + col_w * 3, cr_y - (int)(8.0f * s), GRAY);

        DrawText("Contracts", table_x, cr_y, font22, LIGHTGRAY);
        cr_y += (int)(30.0f * s);

        int font18 = (int)(18.0f * s);
        for (int i = 0; i < NUM_PLAYERS; i++) {
            if (rs->contract_result_text[i][0] == '\0') continue;
            Color cr_col = rs->contract_result_success[i] ? GREEN : RED;
            DrawText(rs->contract_result_text[i], table_x, cr_y, font18, cr_col);
            cr_y += (int)(24.0f * s);
        }
    }

    draw_button(&rs->btn_continue, s);
}

static void draw_phase_game_over(const GameState *gs, const RenderState *rs)
{
    float s = rs->layout.scale;
    const LayoutConfig *cfg = &rs->layout;

    Rectangle br = layout_board_rect(cfg);
    DrawRectangleRec(br, (Color){0, 0, 0, 200});

    Vector2 bc = layout_board_center(cfg);

    const char *title = "Game Over";
    int title_size = (int)(48.0f * s);
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(bc.x - (float)tw * 0.5f),
             (int)(cfg->board_y + 100.0f * s), title_size, GOLD);

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
    DrawText(winner_text, (int)(bc.x - (float)ww * 0.5f),
             (int)(cfg->board_y + 180.0f * s), win_size, RAYWHITE);

    /* Final scores */
    int score_col_w = (int)(150.0f * s);
    int table_w = (int)(300.0f * s);
    int table_x = (int)(cfg->board_x + (cfg->board_size - (float)table_w) * 0.5f);
    int table_y = (int)(cfg->board_y + 260.0f * s);
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
    float screen_cy = rs->layout.screen_height * 0.5f;

    /* Title */
    const char *title = "SETTINGS";
    int title_size = (int)(40.0f * s);
    int tw = MeasureText(title, title_size);
    float title_y = rs->settings_rows_prev[0].bounds.y - 70.0f * s;
    DrawText(title, (int)(screen_cx - (float)tw * 0.5f),
             (int)title_y, title_size, RAYWHITE);

    /* Section headers */
    float row_h = 40.0f * s;
    float row_gap = 8.0f * s;
    float start_y = screen_cy - 180.0f * s;
    int section_fs = (int)(16.0f * s);
    float label_x = screen_cx - 230.0f * s;

    /* Draw section labels */
    DrawText("Display", (int)label_x,
             (int)(start_y - 24.0f * s), section_fs, LIGHTGRAY);
    DrawText("Gameplay", (int)label_x,
             (int)(start_y + 3.0f * (row_h + row_gap) + 16.0f * s - 24.0f * s),
             section_fs, LIGHTGRAY);
    DrawText("Audio", (int)label_x,
             (int)(start_y + 5.0f * (row_h + row_gap) + 32.0f * s - 24.0f * s),
             section_fs, LIGHTGRAY);

    int label_fs = (int)(20.0f * s);
    int value_fs = (int)(20.0f * s);

    /* Draw each setting row */
    for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
        float y = start_y + (float)i * (row_h + row_gap);
        if (i >= 3) y += 16.0f * s;
        if (i >= 5) y += 16.0f * s;

        Color lbl_col = rs->settings_disabled[i]
                            ? (Color){100, 100, 100, 255}
                            : RAYWHITE;
        Color val_col = rs->settings_disabled[i]
                            ? (Color){80, 80, 80, 255}
                            : GOLD;

        /* Label */
        DrawText(rs->settings_labels[i],
                 (int)label_x,
                 (int)(y + (row_h - (float)label_fs) * 0.5f),
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
                     (int)(y + (row_h - (float)value_fs) * 0.5f),
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

/* ---- Main draw ---- */

void render_draw(const GameState *gs, const RenderState *rs)
{
    BeginDrawing();
    ClearBackground((Color){20, 60, 20, 255});

    switch (gs->phase) {
    case PHASE_MENU:
        draw_phase_menu(gs, rs);
        break;

    case PHASE_DEALING:
        break;

    case PHASE_PASSING:
        draw_left_panel_chat(rs);
        draw_left_panel_info(rs);
        draw_phase_passing(gs, rs);
        break;

    case PHASE_PLAYING:
        draw_phase_playing(gs, rs);
        if (rs->grudge_discard_ui) {
            draw_grudge_discard_ui(rs);
        }
        particle_draw(&rs->particles);
        break;

    case PHASE_SCORING:
        draw_left_panel_chat(rs);
        draw_left_panel_info(rs);
        draw_phase_scoring(gs, rs);
        particle_draw(&rs->particles);
        break;

    case PHASE_GAME_OVER:
        draw_phase_game_over(gs, rs);
        break;

    case PHASE_SETTINGS:
        draw_phase_settings(gs, rs);
        break;

    default:
        break;
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
        float top_left_x = pos.x - cv->origin.x;
        float top_left_y = pos.y - cv->origin.y;
        Rectangle card_rect = {top_left_x, top_left_y, w, h};

        if (CheckCollisionPointRec(mouse_pos, card_rect)) {
            return idx;
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

    if (rs->selected_count < PASS_CARD_COUNT) {
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

void render_chat_log_push(RenderState *rs, const char *msg)
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
}

const char *render_effect_label(const ActiveEffect *ae, char *buf, int buflen)
{
    static const char *suit_names[] = {"Clubs", "Diamonds", "Spades", "Hearts"};
    const Effect *e = &ae->effect;
    switch (e->type) {
    case EFFECT_POINTS_PER_HEART:
        snprintf(buf, buflen, "Hearts %+d pt", e->param.points_delta); break;
    case EFFECT_POINTS_FOR_QOS:
        snprintf(buf, buflen, "QoS %+d pt", e->param.points_delta); break;
    case EFFECT_FLAT_SCORE_ADJUST:
        snprintf(buf, buflen, "Score %+d", e->param.points_delta); break;
    case EFFECT_HEARTS_BREAK_EARLY:
        snprintf(buf, buflen, "Hearts pre-broken"); break;
    case EFFECT_VOID_SUIT:
        if (e->param.voided_suit >= 0 && e->param.voided_suit < 4)
            snprintf(buf, buflen, "Void %s", suit_names[e->param.voided_suit]);
        else
            snprintf(buf, buflen, "Void suit");
        break;
    case EFFECT_REVEAL_HAND:
        snprintf(buf, buflen, "Hand revealed"); break;
    case EFFECT_REVEAL_CONTRACT:
        snprintf(buf, buflen, "Contract revealed"); break;
    case EFFECT_SWAP_CARD_POINTS:
        snprintf(buf, buflen, "Swapped points"); break;
    default:
        snprintf(buf, buflen, "Effect"); break;
    }
    return buf;
}
