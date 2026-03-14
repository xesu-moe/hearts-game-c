/* ============================================================
 * @deps-implements: render.h
 * @deps-requires: render.h (MenuItem, UIButton.subtitle, UIButton.disabled),
 *                 anim.h, layout.h, card_render.h, game_state.h, card.h,
 *                 raylib.h, rlgl.h
 * @deps-last-changed: 2026-03-14 — Implemented menu_items array, disabled styling, subtitle text rendering
 * ============================================================ */

#include "render.h"

#include <assert.h>
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

static const LayoutConfig DEFAULT_LAYOUT = {
    .screen_width  = 1280,
    .screen_height = 720,
    .card_width    = CARD_WIDTH,
    .card_height   = CARD_HEIGHT,
    .card_overlap  = CARD_OVERLAP,
    .board_x       = 560,
    .board_y       = 0,
    .board_size    = 720,
};

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
    rs->card_count = 0;
    rs->trick_visual_count = 0;

    for (int p = 0; p < NUM_PLAYERS; p++) {
        const Hand *hand = &gs->players[p].hand;
        rs->hand_visual_counts[p] = hand->count;

        Vector2 positions[MAX_HAND_SIZE];
        float rotations[MAX_HAND_SIZE];
        int count = 0;
        PlayerPosition spos = player_screen_pos(p);
        layout_hand_positions(spos, hand->count, &DEFAULT_LAYOUT,
                              positions, rotations, &count);

        float scale = (p == HUMAN_PLAYER) ? 1.0f : 0.7f;
        float cw_s = CARD_WIDTH * scale;
        float ch_s = CARD_HEIGHT * scale;

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
            cv->scale = scale;
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
        Vector2 trick_pos = layout_trick_position(spos, &DEFAULT_LAYOUT);

        CardVisual *cv = &rs->cards[idx];
        cv->card = trick->cards[i];
        cv->position = trick_pos;
        cv->target = trick_pos;
        cv->start = trick_pos;
        cv->face_up = true;
        cv->scale = 0.85f;
        cv->opacity = 1.0f;
        cv->z_order = 100 + i;
        cv->animating = false;
    }
}

static void sync_buttons(const GameState *gs, RenderState *rs)
{
    Rectangle btn_rect = layout_confirm_button(&DEFAULT_LAYOUT);
    Vector2 bc = layout_board_center(&DEFAULT_LAYOUT);

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
            [MENU_SETTINGS]      = {"Settings",        "(Coming Soon)", true},
            [MENU_EXIT]          = {"Exit",            NULL,            false},
        };

        float btn_w = 280.0f;
        float btn_h = 50.0f;
        float btn_gap = 12.0f;
        float total_h = MENU_ITEM_COUNT * btn_h +
                         (MENU_ITEM_COUNT - 1) * btn_gap;
        float menu_top_y = bc.y - total_h * 0.5f + 40.0f;

        for (int i = 0; i < MENU_ITEM_COUNT; i++) {
            rs->menu_items[i].bounds = (Rectangle){
                bc.x - btn_w * 0.5f,
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
    rs->btn_confirm_pass.visible =
        (gs->phase == PHASE_PASSING && rs->selected_count == PASS_CARD_COUNT);

    if (gs->phase == PHASE_GAME_OVER) {
        rs->btn_continue.bounds = (Rectangle){
            bc.x - 100.0f,
            (float)DEFAULT_LAYOUT.board_y + 460.0f,
            200.0f, 50.0f
        };
        rs->btn_continue.label = "Return to Menu";
        rs->btn_continue.visible = true;
    } else {
        rs->btn_continue.bounds = btn_rect;
        rs->btn_continue.label = "Continue";
        rs->btn_continue.visible = (gs->phase == PHASE_SCORING);
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

    for (int i = 0; i < PASS_CARD_COUNT; i++) {
        rs->selected_indices[i] = -1;
    }
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
            for (int s = 0; s < saved_count; s++) {
                for (int i = 0; i < rs->hand_visual_counts[HUMAN_PLAYER]; i++) {
                    int idx = rs->hand_visuals[HUMAN_PLAYER][i];
                    if (card_equals(rs->cards[idx].card, saved_selected[s])) {
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

            /* Compute hand center as animation start */
            PlayerPosition spos = player_screen_pos(anim_player);
            Vector2 hand_center = layout_trick_position(spos, &DEFAULT_LAYOUT);
            float start_rot = 0.0f;
            /* Use player's hand area center as start, convert pivot to top-left */
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

    /* Update hover state — clear previous frame's hover first */
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

    sync_buttons(gs, rs);
}

/* ---- Drawing helpers ---- */

static void draw_card_visual(const CardVisual *cv)
{
    Vector2 pos = cv->position;

    /* Lift selected cards (upward for bottom player) */
    if (cv->selected) {
        pos.y -= CARD_SELECT_LIFT;
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

static void draw_button(const UIButton *btn)
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

    int font_size = 24;
    int tw = MeasureText(btn->label, font_size);

    if (btn->subtitle != NULL) {
        int sub_size = 14;
        int sw = MeasureText(btn->subtitle, sub_size);
        int total_h = font_size + 4 + sub_size;
        int y_start = (int)(btn->bounds.y +
                            (btn->bounds.height - (float)total_h) * 0.5f);

        DrawText(btn->label,
                 (int)(btn->bounds.x + (btn->bounds.width - (float)tw) * 0.5f),
                 y_start, font_size, text_col);

        Color sub_col = btn->disabled ? (Color){100, 100, 100, 255} : LIGHTGRAY;
        DrawText(btn->subtitle,
                 (int)(btn->bounds.x + (btn->bounds.width - (float)sw) * 0.5f),
                 y_start + font_size + 4, sub_size, sub_col);
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
    Vector2 bc = layout_board_center(&DEFAULT_LAYOUT);

    /* Title and subtitle positioned above the first menu item */
    float title_y = rs->menu_items[0].bounds.y - 90.0f;

    const char *title = "HOLLOW HEARTS";
    int title_size = 50;
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(bc.x - (float)tw * 0.5f),
             (int)title_y, title_size, RAYWHITE);

    const char *subtitle = "A deck-building Hearts modification";
    int sub_size = 20;
    int sw = MeasureText(subtitle, sub_size);
    DrawText(subtitle, (int)(bc.x - (float)sw * 0.5f),
             (int)(title_y + (float)title_size + 10.0f), sub_size, LIGHTGRAY);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        draw_button(&rs->menu_items[i]);
    }
}

static void draw_phase_passing(const GameState *gs, const RenderState *rs)
{
    /* Pass direction indicator */
    Vector2 dir_pos = layout_pass_direction_position(&DEFAULT_LAYOUT);
    const char *dir_str = pass_direction_string(gs->pass_direction);
    int dir_size = 28;
    int dw = MeasureText(dir_str, dir_size);
    DrawText(dir_str, (int)(dir_pos.x + (160.0f - (float)dw) * 0.5f),
             (int)dir_pos.y, dir_size, GOLD);

    /* Selection count */
    char sel_text[32];
    snprintf(sel_text, sizeof(sel_text), "Selected: %d / %d",
             rs->selected_count, PASS_CARD_COUNT);
    int sel_size = 20;
    int sel_w = MeasureText(sel_text, sel_size);
    Vector2 bc = layout_board_center(&DEFAULT_LAYOUT);
    DrawText(sel_text, (int)(bc.x - (float)sel_w * 0.5f),
             DEFAULT_LAYOUT.board_y + 270, sel_size, LIGHTGRAY);

    /* Draw all hands */
    for (int i = 0; i < rs->card_count; i++) {
        draw_card_visual(&rs->cards[i]);
    }

    draw_button(&rs->btn_confirm_pass);
}

static void draw_phase_playing(const GameState *gs, const RenderState *rs)
{
    /* Draw hands */
    for (int i = 0; i < rs->card_count; i++) {
        draw_card_visual(&rs->cards[i]);
    }

    /* Current player indicator */
    int current = game_state_current_player(gs);
    if (current >= 0) {
        PlayerPosition spos = player_screen_pos(current);
        Vector2 name_pos = layout_name_position(spos, &DEFAULT_LAYOUT);
        const char *name = player_name(current);
        Color col = (current == HUMAN_PLAYER) ? GREEN : YELLOW;
        DrawText(name, (int)name_pos.x, (int)name_pos.y, 18, col);
    }

    /* Hearts broken indicator */
    if (gs->hearts_broken) {
        Vector2 bc = layout_board_center(&DEFAULT_LAYOUT);
        DrawText("Hearts Broken!", (int)(bc.x - 70.0f),
                 DEFAULT_LAYOUT.board_y + 30, 20, RED);
    }

    /* Trick count */
    char trick_text[32];
    snprintf(trick_text, sizeof(trick_text), "Trick %d/13", gs->tricks_played + 1);
    DrawText(trick_text, DEFAULT_LAYOUT.board_x + DEFAULT_LAYOUT.board_size - 120,
             DEFAULT_LAYOUT.board_y + 30, 18, LIGHTGRAY);

    /* Dim unplayable cards for human player during their turn */
    if (current == HUMAN_PLAYER) {
        for (int i = 0; i < rs->hand_visual_counts[HUMAN_PLAYER]; i++) {
            int idx = rs->hand_visuals[HUMAN_PLAYER][i];
            const CardVisual *cv = &rs->cards[idx];
            if (!game_state_is_valid_play(gs, HUMAN_PLAYER, cv->card)) {
                Vector2 pos = cv->position;
                float w = CARD_WIDTH * cv->scale;
                float h = CARD_HEIGHT * cv->scale;

                /* Use rlgl transform to match card rotation */
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
    /* Semi-transparent overlay over board area */
    Rectangle br = layout_board_rect(&DEFAULT_LAYOUT);
    DrawRectangleRec(br, (Color){0, 0, 0, 150});

    Vector2 bc = layout_board_center(&DEFAULT_LAYOUT);

    const char *title = "Round Complete";
    int title_size = 36;
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(bc.x - (float)tw * 0.5f),
             DEFAULT_LAYOUT.board_y + 100, title_size, RAYWHITE);

    /* Score table */
    int col_w = 150;
    int table_x = DEFAULT_LAYOUT.board_x + (DEFAULT_LAYOUT.board_size - col_w * 3) / 2;
    int table_y = DEFAULT_LAYOUT.board_y + 180;
    int row_h = 40;

    /* Header */
    DrawText("Player", table_x, table_y, 22, LIGHTGRAY);
    DrawText("Round", table_x + col_w, table_y, 22, LIGHTGRAY);
    DrawText("Total", table_x + col_w * 2, table_y, 22, LIGHTGRAY);

    DrawLine(table_x, table_y + 28, table_x + col_w * 3, table_y + 28, GRAY);

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

        DrawText(player_name(i), table_x, y, 22, row_col);

        char pts[16];
        snprintf(pts, sizeof(pts), "%d", rs->displayed_round_points[i]);
        DrawText(pts, table_x + col_w, y, 22, row_col);

        snprintf(pts, sizeof(pts), "%d", rs->displayed_total_scores[i]);
        DrawText(pts, table_x + col_w * 2, y, 22, row_col);
    }

    draw_button(&rs->btn_continue);
}

static void draw_phase_game_over(const GameState *gs, const RenderState *rs)
{
    /* Dark overlay over board area */
    Rectangle br = layout_board_rect(&DEFAULT_LAYOUT);
    DrawRectangleRec(br, (Color){0, 0, 0, 200});

    Vector2 bc = layout_board_center(&DEFAULT_LAYOUT);

    const char *title = "Game Over";
    int title_size = 48;
    int tw = MeasureText(title, title_size);
    DrawText(title, (int)(bc.x - (float)tw * 0.5f),
             DEFAULT_LAYOUT.board_y + 100, title_size, GOLD);

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
    int win_size = 36;
    int ww = MeasureText(winner_text, win_size);
    DrawText(winner_text, (int)(bc.x - (float)ww * 0.5f),
             DEFAULT_LAYOUT.board_y + 180, win_size, RAYWHITE);

    /* Final scores */
    int table_x = DEFAULT_LAYOUT.board_x + (DEFAULT_LAYOUT.board_size - 300) / 2;
    int table_y = DEFAULT_LAYOUT.board_y + 260;
    int row_h = 35;

    DrawText("Player", table_x, table_y, 22, LIGHTGRAY);
    DrawText("Score", table_x + 150, table_y, 22, LIGHTGRAY);
    DrawLine(table_x, table_y + 28, table_x + 300, table_y + 28, GRAY);

    for (int i = 0; i < NUM_PLAYERS; i++) {
        int y = table_y + row_h * (i + 1);
        Color col = RAYWHITE;
        for (int w = 0; w < win_count; w++) {
            if (winners[w] == i) { col = GREEN; break; }
        }

        DrawText(player_name(i), table_x, y, 22, col);

        char pts[16];
        snprintf(pts, sizeof(pts), "%d", gs->players[i].total_score);
        DrawText(pts, table_x + 150, y, 22, col);
    }

    draw_button(&rs->btn_continue);
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
        /* Dealing is instant — fall through to playing */
        break;

    case PHASE_PASSING:
        draw_phase_passing(gs, rs);
        break;

    case PHASE_PLAYING:
        draw_phase_playing(gs, rs);
        break;

    case PHASE_SCORING:
        draw_phase_scoring(gs, rs);
        break;

    case PHASE_GAME_OVER:
        draw_phase_game_over(gs, rs);
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
    /* Only test human player's hand, in reverse z-order (topmost first) */
    for (int i = rs->hand_visual_counts[HUMAN_PLAYER] - 1; i >= 0; i--) {
        int idx = rs->hand_visuals[HUMAN_PLAYER][i];
        const CardVisual *cv = &rs->cards[idx];

        Vector2 pos = cv->position;
        if (cv->selected) {
            pos.y -= CARD_SELECT_LIFT;
        }

        /* Convert pivot-based position to top-left for AABB hit test.
         * Rotation is small enough that AABB approximation is fine. */
        float w = CARD_WIDTH * cv->scale;
        float h = CARD_HEIGHT * cv->scale;
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
    /* Check if already selected */
    for (int i = 0; i < rs->selected_count; i++) {
        if (rs->selected_indices[i] == card_visual_index) {
            /* Deselect: shift remaining */
            rs->cards[card_visual_index].selected = false;
            for (int j = i; j < rs->selected_count - 1; j++) {
                rs->selected_indices[j] = rs->selected_indices[j + 1];
            }
            rs->selected_count--;
            rs->selected_indices[rs->selected_count] = -1;
            return rs->selected_count;
        }
    }

    /* Select if room */
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
