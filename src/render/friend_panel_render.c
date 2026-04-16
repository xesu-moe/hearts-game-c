#include "friend_panel_render.h"
#include "render.h"
#include "../net/lobby_client.h"
#include <raylib.h>
#include <string.h>
#include <stdio.h>

/* Colors — green-tinted theme matching transmute tooltips */
#define COL_PANEL_BG       CLITERAL(Color){20, 30, 20, 240}
#define COL_PANEL_BORDER   CLITERAL(Color){180, 160, 80, 200}
#define COL_ENTRY_BG       CLITERAL(Color){30, 40, 30, 255}
#define COL_ENTRY_HOVER    CLITERAL(Color){40, 55, 40, 255}
#define COL_TEXT            CLITERAL(Color){220, 220, 220, 255}
#define COL_TEXT_DIM        CLITERAL(Color){140, 140, 160, 255}
#define COL_GREEN_DOT      CLITERAL(Color){80, 220, 80, 255}
#define COL_YELLOW_DOT     CLITERAL(Color){220, 200, 50, 255}
#define COL_BTN_ACCEPT     CLITERAL(Color){50, 150, 80, 255}
#define COL_BTN_REJECT     CLITERAL(Color){180, 50, 50, 255}
#define COL_BTN_INVITE     CLITERAL(Color){50, 100, 180, 255}
#define COL_BTN_ADD        CLITERAL(Color){50, 150, 80, 255}
#define COL_BTN_BLOCKED    CLITERAL(Color){80, 80, 80, 255}
#define COL_SEARCH_BG      CLITERAL(Color){15, 25, 15, 255}
#define COL_LABEL_REQUEST  CLITERAL(Color){220, 180, 50, 255}
#define COL_LABEL_INVITE   CLITERAL(Color){100, 180, 255, 255}
#define COL_CONTEXT_BG     CLITERAL(Color){30, 45, 30, 250}
#define COL_CONFIRM_BG     CLITERAL(Color){25, 35, 25, 250}
#define COL_OVERLAY        CLITERAL(Color){0, 0, 0, 150}

#define TITLE_HEIGHT   36
#define BTN_W          70
#define BTN_H          28
#define DOT_RADIUS     4
#define FONT_SIZE      26
#define FONT_SM        20
#define FONT_SPACING   1

/* ================================================================
 * Button helper
 * ================================================================ */

static bool draw_button(const RenderState *rs, const char *text, Rectangle rect, Color bg, Color tcol)
{
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, rect);
    Color c = hover ? (Color){(uint8_t)(bg.r + 20 > 255 ? 255 : bg.r + 20),
                               (uint8_t)(bg.g + 20 > 255 ? 255 : bg.g + 20),
                               (uint8_t)(bg.b + 20 > 255 ? 255 : bg.b + 20), bg.a} : bg;
    DrawRectangleRec(rect, c);
    int fs = (int)(rect.height * 0.7f); /* scale font to button height */
    if (fs < 8) fs = 8;
    int tw = hh_measure_text(rs, text, fs);
    int tx = (int)(rect.x + (rect.width - (float)tw) / 2.0f);
    int ty = (int)(rect.y + (rect.height - fs) / 2.0f);
    hh_draw_text(rs, text, tx, ty, fs, tcol);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* Check if mouse is in rect (no click) */
static bool is_hovered(Rectangle rect)
{
    return CheckCollisionPointRec(GetMousePosition(), rect);
}

/* ================================================================
 * Draw
 * ================================================================ */

void friend_panel_render_draw(FriendPanelState *state, Rectangle pr,
                              const RenderState *rs)
{
    float s = pr.width / (float)FRIEND_PANEL_WIDTH;
    float title_h   = TITLE_HEIGHT * s;
    float entry_h   = FRIEND_ENTRY_HEIGHT * s;
    float search_h  = FRIEND_SEARCH_HEIGHT * s;
    float btn_w     = BTN_W * s;
    float btn_h     = BTN_H * s;
    float dot_r     = DOT_RADIUS * s;
    int   font_lg   = (int)(FONT_SIZE * s);
    int   font_sm   = (int)(FONT_SM * s);
    int   font_title = (int)((FONT_SIZE + 2) * s);

    /* Panel background (rounded, matching transmute tooltip style) */
    DrawRectangleRounded(pr, 0.05f, 4, COL_PANEL_BG);
    DrawRectangleRoundedLines(pr, 0.05f, 4, COL_PANEL_BORDER);

    /* Title */
    {
        const char *title = "Friends";
        int tw = hh_measure_text(rs, title, font_title);
        hh_draw_text(rs, title, (int)(pr.x + (pr.width - (float)tw) / 2.0f),
                     (int)(pr.y + 4 * s), font_title, COL_TEXT);
    }

    /* Yellow separator line below title, inset from borders */
    {
        float sep_y = pr.y + title_h - 4 * s;
        float margin = 8.0f * s;
        DrawLineEx((Vector2){pr.x + margin, sep_y},
                   (Vector2){pr.x + pr.width - margin, sep_y},
                   1.0f, COL_PANEL_BORDER);
    }

    float content_top = pr.y + title_h;
    float search_top = pr.y + pr.height - search_h;
    float content_h = search_top - content_top;

    /* Scrollable entries area */
    BeginScissorMode((int)pr.x, (int)content_top, (int)pr.width, (int)content_h);

    for (int i = 0; i < state->entry_count; i++) {
        FriendEntry *e = &state->entries[i];
        float ey = content_top + i * entry_h - state->scroll_offset;

        /* Skip if fully outside visible area */
        if (ey + entry_h < content_top || ey > search_top) continue;

        Rectangle er = {pr.x + 2 * s, ey, pr.width - 4 * s, entry_h - 2 * s};
        bool hover = is_hovered(er);
        DrawRectangleRec(er, hover ? COL_ENTRY_HOVER : COL_ENTRY_BG);

        float tx = er.x + 4 * s;
        float btn_x = er.x + er.width; /* buttons grow leftward from right edge */

        if (e->type == FRIEND_ENTRY_INVITE) {
            hh_draw_text(rs, "Room Invitation", (int)tx, (int)(ey + 2 * s), font_sm, COL_LABEL_INVITE);
            hh_draw_text(rs, e->username, (int)tx, (int)(ey + 14 * s), font_sm, COL_TEXT);
            Rectangle ar = {btn_x - btn_w * 2 - 6 * s, ey + 8 * s, btn_w, btn_h};
            Rectangle rr = {btn_x - btn_w - 2 * s, ey + 8 * s, btn_w, btn_h};
            draw_button(rs, "Accept", ar, COL_BTN_ACCEPT, COL_TEXT);
            draw_button(rs, "Reject", rr, COL_BTN_REJECT, COL_TEXT);

        } else if (e->type == FRIEND_ENTRY_REQUEST) {
            hh_draw_text(rs, "Friend Request", (int)tx, (int)(ey + 2 * s), font_sm, COL_LABEL_REQUEST);
            hh_draw_text(rs, e->username, (int)tx, (int)(ey + 14 * s), font_sm, COL_TEXT);
            Rectangle ar = {btn_x - btn_w * 2 - 6 * s, ey + 8 * s, btn_w, btn_h};
            Rectangle rr = {btn_x - btn_w - 2 * s, ey + 8 * s, btn_w, btn_h};
            draw_button(rs, "Accept", ar, COL_BTN_ACCEPT, COL_TEXT);
            draw_button(rs, "Reject", rr, COL_BTN_REJECT, COL_TEXT);

        } else { /* FRIEND_ENTRY_FRIEND */
            float dot_y = ey + entry_h / 2.0f;
            if (e->presence == FRIEND_PRESENCE_ONLINE) {
                DrawCircle((int)(tx + dot_r), (int)dot_y, dot_r, COL_GREEN_DOT);
                tx += dot_r * 2 + 4 * s;
            } else if (e->presence == FRIEND_PRESENCE_IN_GAME) {
                DrawCircle((int)(tx + dot_r), (int)dot_y, dot_r, COL_YELLOW_DOT);
                tx += dot_r * 2 + 4 * s;
            }
            Color ncol = (e->presence == FRIEND_PRESENCE_OFFLINE) ? COL_TEXT_DIM : COL_TEXT;
            hh_draw_text(rs, e->username, (int)tx,
                         (int)(ey + (entry_h - font_lg) / 2.0f),
                         font_lg, ncol);
            if (state->can_invite && e->presence == FRIEND_PRESENCE_ONLINE) {
                Rectangle ir = {btn_x - btn_w - 2 * s, ey + 8 * s, btn_w, btn_h};
                draw_button(rs, "Invite", ir, COL_BTN_INVITE, COL_TEXT);
            }
        }
    }

    EndScissorMode();

    /* ---- Search bar ---- */
    {
        Rectangle sb = {pr.x + 2 * s, search_top + 2 * s, pr.width - 4 * s, search_h - 4 * s};
        DrawRectangleRec(sb, COL_SEARCH_BG);
        if (state->search_active) {
            DrawRectangleLinesEx(sb, 1, COL_LABEL_INVITE);
        }

        const char *display = (state->search_len > 0) ? state->search_buf : "Search players...";
        Color tc = (state->search_len > 0) ? COL_TEXT : COL_TEXT_DIM;
        hh_draw_text(rs, display, (int)(sb.x + 6 * s),
                     (int)(sb.y + (sb.height - font_sm) / 2.0f), font_sm, tc);
    }

    /* ---- Search results (to the right of panel, growing upward) ---- */
    if (state->search_results_visible && state->search_result_count > 0) {
        int shown = 0;
        for (int i = 0; i < state->search_result_count; i++) {
            if (state->search_results[i].status == FRIEND_STATUS_SELF) continue;
            shown++;
        }
        float row_h = 28.0f * s;
        float results_h = shown * row_h + 4 * s;
        float results_w = pr.width;
        float rx = pr.x + pr.width + 4 * s;
        float ry = search_top + search_h - results_h;
        Rectangle rbg = {rx, ry, results_w, results_h};
        DrawRectangleRounded(rbg, 0.05f, 4, COL_PANEL_BG);
        DrawRectangleRoundedLines(rbg, 0.05f, 4, COL_PANEL_BORDER);

        float cy = ry + 2 * s;
        for (int i = 0; i < state->search_result_count; i++) {
            if (state->search_results[i].status == FRIEND_STATUS_SELF) continue;

            Rectangle row = {rx + 2 * s, cy, results_w - 4 * s, 26 * s};
            DrawRectangleRec(row, COL_ENTRY_BG);
            hh_draw_text(rs, state->search_results[i].username,
                         (int)(row.x + 4 * s), (int)(cy + 6 * s), font_sm, COL_TEXT);

            Rectangle br = {row.x + row.width - btn_w - 2 * s, cy + 3 * s, btn_w, 20 * s};
            uint8_t st = state->search_results[i].status;
            if (st == FRIEND_STATUS_AVAILABLE) {
                draw_button(rs, "Add", br, COL_BTN_ADD, COL_TEXT);
            } else if (st == FRIEND_STATUS_ALREADY_FRIEND) {
                draw_button(rs, "Friends", br, COL_BTN_BLOCKED, COL_TEXT_DIM);
            } else if (st == FRIEND_STATUS_PENDING_SENT || st == FRIEND_STATUS_PENDING_RECEIVED) {
                draw_button(rs, "Pending", br, COL_BTN_BLOCKED, COL_TEXT_DIM);
            } else if (st == FRIEND_STATUS_BLOCKED) {
                draw_button(rs, "Blocked", br, COL_BTN_BLOCKED, COL_TEXT_DIM);
            }
            cy += row_h;
        }
    }

    /* ---- Context menu ---- */
    if (state->context_menu_open) {
        Rectangle cm = {state->context_menu_x, state->context_menu_y, 120 * s, 28 * s};
        DrawRectangleRec(cm, COL_CONTEXT_BG);
        DrawRectangleLinesEx(cm, 1, COL_TEXT_DIM);
        draw_button(rs, "Remove Friend", cm, COL_CONTEXT_BG, COL_TEXT);
    }

    /* ---- Confirmation dialog ---- */
    if (state->confirm_remove_open && state->confirm_remove_entry >= 0 &&
        state->confirm_remove_entry < state->entry_count) {
        DrawRectangleRec(pr, COL_OVERLAY);

        float dw = 180 * s, dh = 80 * s;
        Rectangle dlg = {pr.x + (pr.width - dw) / 2.0f,
                         pr.y + (pr.height - dh) / 2.0f, dw, dh};
        DrawRectangleRec(dlg, COL_CONFIRM_BG);
        DrawRectangleLinesEx(dlg, 1, COL_TEXT_DIM);

        char prompt[64];
        snprintf(prompt, sizeof(prompt), "Remove %s?",
                 state->entries[state->confirm_remove_entry].username);
        int pw = hh_measure_text(rs, prompt, font_sm);
        hh_draw_text(rs, prompt, (int)(dlg.x + (dlg.width - (float)pw) / 2.0f),
                     (int)(dlg.y + 12 * s), font_sm, COL_TEXT);

        Rectangle yes_r = {dlg.x + 20 * s, dlg.y + dh - 30 * s, 60 * s, 22 * s};
        Rectangle no_r = {dlg.x + dw - 80 * s, dlg.y + dh - 30 * s, 60 * s, 22 * s};
        draw_button(rs, "Yes", yes_r, COL_BTN_REJECT, COL_TEXT);
        draw_button(rs, "No", no_r, COL_BTN_ACCEPT, COL_TEXT);
    }
}

/* ================================================================
 * Input
 * ================================================================ */

void friend_panel_render_input(FriendPanelState *state, Rectangle pr)
{
    float s = pr.width / (float)FRIEND_PANEL_WIDTH;
    float title_h  = TITLE_HEIGHT * s;
    float entry_h  = FRIEND_ENTRY_HEIGHT * s;
    float search_h = FRIEND_SEARCH_HEIGHT * s;
    float btn_w    = BTN_W * s;
    float btn_h    = BTN_H * s;

    Vector2 mouse = GetMousePosition();
    bool in_panel = CheckCollisionPointRec(mouse, pr);

    float content_top = pr.y + title_h;
    float search_top = pr.y + pr.height - search_h;

    /* ---- Confirmation dialog input (takes priority) ---- */
    if (state->confirm_remove_open) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            float dw = 180 * s, dh = 80 * s;
            Rectangle dlg = {pr.x + (pr.width - dw) / 2.0f,
                             pr.y + (pr.height - dh) / 2.0f, dw, dh};
            Rectangle yes_r = {dlg.x + 20 * s, dlg.y + dh - 30 * s, 60 * s, 22 * s};
            Rectangle no_r = {dlg.x + dw - 80 * s, dlg.y + dh - 30 * s, 60 * s, 22 * s};

            if (CheckCollisionPointRec(mouse, yes_r)) {
                int idx = state->confirm_remove_entry;
                if (idx >= 0 && idx < state->entry_count) {
                    lobby_client_friend_remove(state->entries[idx].account_id);
                    for (int j = idx; j < state->entry_count - 1; j++)
                        state->entries[j] = state->entries[j + 1];
                    state->entry_count--;
                }
                state->confirm_remove_open = false;
            } else if (CheckCollisionPointRec(mouse, no_r)) {
                state->confirm_remove_open = false;
            }
        }
        return;
    }

    /* ---- Context menu input ---- */
    if (state->context_menu_open) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Rectangle cm = {state->context_menu_x, state->context_menu_y, 120 * s, 28 * s};
            if (CheckCollisionPointRec(mouse, cm)) {
                state->confirm_remove_open = true;
                state->confirm_remove_entry = state->context_menu_entry;
            }
            state->context_menu_open = false;
        }
        return;
    }

    /* ---- Scroll ---- */
    if (in_panel) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            state->scroll_target -= wheel * entry_h;
            float max_scroll = state->entry_count * entry_h - (search_top - content_top);
            if (max_scroll < 0) max_scroll = 0;
            if (state->scroll_target < 0) state->scroll_target = 0;
            if (state->scroll_target > max_scroll) state->scroll_target = max_scroll;
        }
    }

    /* ---- Search bar click / text input ---- */
    Rectangle sb = {pr.x + 2 * s, search_top + 2 * s, pr.width - 4 * s, search_h - 4 * s};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(mouse, sb)) {
            state->search_active = true;
        } else {
            state->search_active = false;
            if (state->search_results_visible) {
                int shown = 0;
                for (int i = 0; i < state->search_result_count; i++)
                    if (state->search_results[i].status != FRIEND_STATUS_SELF) shown++;
                float row_h = 28.0f * s;
                float results_h = shown * row_h + 4 * s;
                float results_w = pr.width;
                float rx = pr.x + pr.width + 4 * s;
                float ry = search_top + search_h - results_h;
                Rectangle rbg = {rx, ry, results_w, results_h};
                if (!CheckCollisionPointRec(mouse, rbg)) {
                    state->search_results_visible = false;
                }
            }
        }
    }

    if (state->search_active) {
        bool text_changed = false;
        int ch = GetCharPressed();
        while (ch > 0) {
            if (ch >= 32 && ch < 127 && state->search_len < 31) {
                state->search_buf[state->search_len++] = (char)ch;
                state->search_buf[state->search_len] = '\0';
                text_changed = true;
            }
            ch = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && state->search_len > 0) {
            state->search_len--;
            state->search_buf[state->search_len] = '\0';
            text_changed = true;
        }
        if (text_changed) {
            if (state->search_len >= 4) {
                lobby_client_friend_search(state->search_buf);
            } else {
                state->search_results_visible = false;
                state->search_result_count = 0;
            }
        }
    }

    /* ---- Search result button clicks ---- */
    if (state->search_results_visible && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int shown = 0;
        for (int i = 0; i < state->search_result_count; i++)
            if (state->search_results[i].status != FRIEND_STATUS_SELF) shown++;
        float row_h = 28.0f * s;
        float results_h = shown * row_h + 4 * s;
        float results_w = pr.width;
        float rx = pr.x + pr.width + 4 * s;
        float ry = search_top + search_h - results_h;
        float cy = ry + 2 * s;
        for (int i = 0; i < state->search_result_count; i++) {
            if (state->search_results[i].status == FRIEND_STATUS_SELF) continue;
            Rectangle br = {rx + results_w - btn_w - 4 * s, cy + 3 * s, btn_w, 20 * s};
            if (state->search_results[i].status == FRIEND_STATUS_AVAILABLE &&
                CheckCollisionPointRec(mouse, br)) {
                lobby_client_friend_request(state->search_results[i].account_id);
                state->search_results[i].status = FRIEND_STATUS_PENDING_SENT;
            }
            cy += row_h;
        }
    }

    /* ---- Entry button clicks ---- */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && in_panel) {
        for (int i = 0; i < state->entry_count; i++) {
            float ey = content_top + i * entry_h - state->scroll_offset;
            if (ey + entry_h < content_top || ey > search_top) continue;

            Rectangle er = {pr.x + 2 * s, ey, pr.width - 4 * s, entry_h - 2 * s};
            if (!CheckCollisionPointRec(mouse, er)) continue;

            float btn_x_pos = er.x + er.width;

            if (state->entries[i].type == FRIEND_ENTRY_INVITE ||
                state->entries[i].type == FRIEND_ENTRY_REQUEST) {
                Rectangle ar = {btn_x_pos - btn_w * 2 - 6 * s, ey + 8 * s, btn_w, btn_h};
                Rectangle rr = {btn_x_pos - btn_w - 2 * s, ey + 8 * s, btn_w, btn_h};

                if (CheckCollisionPointRec(mouse, ar)) {
                    if (state->entries[i].type == FRIEND_ENTRY_REQUEST) {
                        lobby_client_friend_accept(state->entries[i].account_id);
                    } else {
                        lobby_client_join_room(state->entries[i].room_code);
                    }
                    for (int j = i; j < state->entry_count - 1; j++)
                        state->entries[j] = state->entries[j + 1];
                    state->entry_count--;
                    break;
                }
                if (CheckCollisionPointRec(mouse, rr)) {
                    if (state->entries[i].type == FRIEND_ENTRY_REQUEST) {
                        lobby_client_friend_reject(state->entries[i].account_id);
                    }
                    for (int j = i; j < state->entry_count - 1; j++)
                        state->entries[j] = state->entries[j + 1];
                    state->entry_count--;
                    break;
                }
            }

            if (state->entries[i].type == FRIEND_ENTRY_FRIEND &&
                state->can_invite &&
                state->entries[i].presence == FRIEND_PRESENCE_ONLINE) {
                Rectangle ir = {btn_x_pos - btn_w - 2 * s, ey + 8 * s, btn_w, btn_h};
                if (CheckCollisionPointRec(mouse, ir)) {
                    lobby_client_room_invite(state->entries[i].account_id, state->current_room_code);
                    break;
                }
            }
        }
    }

    /* ---- Right-click context menu on friends ---- */
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && in_panel) {
        for (int i = 0; i < state->entry_count; i++) {
            if (state->entries[i].type != FRIEND_ENTRY_FRIEND) continue;
            float ey = content_top + i * entry_h - state->scroll_offset;
            if (ey + entry_h < content_top || ey > search_top) continue;
            Rectangle er = {pr.x + 2 * s, ey, pr.width - 4 * s, entry_h - 2 * s};
            if (CheckCollisionPointRec(mouse, er)) {
                state->context_menu_open = true;
                state->context_menu_entry = i;
                state->context_menu_x = mouse.x;
                state->context_menu_y = mouse.y;
                break;
            }
        }
    }
}
