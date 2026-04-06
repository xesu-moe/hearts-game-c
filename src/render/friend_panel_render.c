#include "friend_panel_render.h"
#include "../net/lobby_client.h"
#include <raylib.h>
#include <string.h>
#include <stdio.h>

/* Colors */
#define COL_PANEL_BG       CLITERAL(Color){30, 30, 40, 240}
#define COL_ENTRY_BG       CLITERAL(Color){40, 40, 55, 255}
#define COL_ENTRY_HOVER    CLITERAL(Color){50, 50, 70, 255}
#define COL_TEXT            CLITERAL(Color){220, 220, 220, 255}
#define COL_TEXT_DIM        CLITERAL(Color){140, 140, 160, 255}
#define COL_GREEN_DOT      CLITERAL(Color){80, 220, 80, 255}
#define COL_YELLOW_DOT     CLITERAL(Color){220, 200, 50, 255}
#define COL_BTN_ACCEPT     CLITERAL(Color){50, 150, 80, 255}
#define COL_BTN_REJECT     CLITERAL(Color){180, 50, 50, 255}
#define COL_BTN_INVITE     CLITERAL(Color){50, 100, 180, 255}
#define COL_BTN_ADD        CLITERAL(Color){50, 150, 80, 255}
#define COL_BTN_BLOCKED    CLITERAL(Color){80, 80, 80, 255}
#define COL_SEARCH_BG      CLITERAL(Color){25, 25, 35, 255}
#define COL_LABEL_REQUEST  CLITERAL(Color){220, 180, 50, 255}
#define COL_LABEL_INVITE   CLITERAL(Color){100, 180, 255, 255}
#define COL_CONTEXT_BG     CLITERAL(Color){50, 50, 65, 250}
#define COL_CONFIRM_BG     CLITERAL(Color){40, 40, 55, 250}
#define COL_OVERLAY        CLITERAL(Color){0, 0, 0, 150}

#define TITLE_HEIGHT   24
#define BTN_W          50
#define BTN_H          20
#define DOT_RADIUS     4
#define FONT_SIZE      14
#define FONT_SM        11
#define FONT_SPACING   1

/* ================================================================
 * Button helper
 * ================================================================ */

static bool draw_button(Font font, const char *text, Rectangle rect, Color bg, Color tcol)
{
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, rect);
    Color c = hover ? (Color){(uint8_t)(bg.r + 20 > 255 ? 255 : bg.r + 20),
                               (uint8_t)(bg.g + 20 > 255 ? 255 : bg.g + 20),
                               (uint8_t)(bg.b + 20 > 255 ? 255 : bg.b + 20), bg.a} : bg;
    DrawRectangleRec(rect, c);
    Vector2 tsz = MeasureTextEx(font, text, FONT_SM, FONT_SPACING);
    Vector2 tpos = {rect.x + (rect.width - tsz.x) / 2.0f,
                    rect.y + (rect.height - tsz.y) / 2.0f};
    DrawTextEx(font, text, tpos, FONT_SM, FONT_SPACING, tcol);
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

void friend_panel_render_draw(FriendPanelState *state, Rectangle pr, Font font)
{
    /* Panel background */
    DrawRectangleRec(pr, COL_PANEL_BG);

    /* Title */
    {
        const char *title = "Friends";
        Vector2 tsz = MeasureTextEx(font, title, FONT_SIZE + 2, FONT_SPACING);
        Vector2 tpos = {pr.x + (pr.width - tsz.x) / 2.0f, pr.y + 4};
        DrawTextEx(font, title, tpos, FONT_SIZE + 2, FONT_SPACING, COL_TEXT);
    }

    float content_top = pr.y + TITLE_HEIGHT;
    float search_top = pr.y + pr.height - FRIEND_SEARCH_HEIGHT;
    float content_h = search_top - content_top;

    /* Scrollable entries area */
    BeginScissorMode((int)pr.x, (int)content_top, (int)pr.width, (int)content_h);

    for (int i = 0; i < state->entry_count; i++) {
        FriendEntry *e = &state->entries[i];
        float ey = content_top + i * FRIEND_ENTRY_HEIGHT - state->scroll_offset;

        /* Skip if fully outside visible area */
        if (ey + FRIEND_ENTRY_HEIGHT < content_top || ey > search_top) continue;

        Rectangle er = {pr.x + 2, ey, pr.width - 4, FRIEND_ENTRY_HEIGHT - 2};
        bool hover = is_hovered(er);
        DrawRectangleRec(er, hover ? COL_ENTRY_HOVER : COL_ENTRY_BG);

        float tx = er.x + 4;
        float btn_x = er.x + er.width; /* buttons grow leftward from right edge */

        if (e->type == FRIEND_ENTRY_INVITE) {
            /* Label */
            DrawTextEx(font, "Room Invitation", (Vector2){tx, ey + 2}, FONT_SM - 1, FONT_SPACING, COL_LABEL_INVITE);
            /* Username */
            DrawTextEx(font, e->username, (Vector2){tx, ey + 14}, FONT_SM, FONT_SPACING, COL_TEXT);
            /* Accept / Reject buttons */
            Rectangle ar = {btn_x - BTN_W * 2 - 6, ey + 8, BTN_W, BTN_H};
            Rectangle rr = {btn_x - BTN_W - 2, ey + 8, BTN_W, BTN_H};
            draw_button(font, "Accept", ar, COL_BTN_ACCEPT, COL_TEXT);
            draw_button(font, "Reject", rr, COL_BTN_REJECT, COL_TEXT);

        } else if (e->type == FRIEND_ENTRY_REQUEST) {
            /* Label */
            DrawTextEx(font, "Friend Request", (Vector2){tx, ey + 2}, FONT_SM - 1, FONT_SPACING, COL_LABEL_REQUEST);
            /* Username */
            DrawTextEx(font, e->username, (Vector2){tx, ey + 14}, FONT_SM, FONT_SPACING, COL_TEXT);
            /* Accept / Reject buttons */
            Rectangle ar = {btn_x - BTN_W * 2 - 6, ey + 8, BTN_W, BTN_H};
            Rectangle rr = {btn_x - BTN_W - 2, ey + 8, BTN_W, BTN_H};
            draw_button(font, "Accept", ar, COL_BTN_ACCEPT, COL_TEXT);
            draw_button(font, "Reject", rr, COL_BTN_REJECT, COL_TEXT);

        } else { /* FRIEND_ENTRY_FRIEND */
            /* Presence dot */
            float dot_y = ey + FRIEND_ENTRY_HEIGHT / 2.0f;
            if (e->presence == FRIEND_PRESENCE_ONLINE) {
                DrawCircle((int)(tx + DOT_RADIUS), (int)dot_y, DOT_RADIUS, COL_GREEN_DOT);
                tx += DOT_RADIUS * 2 + 4;
            } else if (e->presence == FRIEND_PRESENCE_IN_GAME) {
                DrawCircle((int)(tx + DOT_RADIUS), (int)dot_y, DOT_RADIUS, COL_YELLOW_DOT);
                tx += DOT_RADIUS * 2 + 4;
            }
            /* Username */
            Color ncol = (e->presence == FRIEND_PRESENCE_OFFLINE) ? COL_TEXT_DIM : COL_TEXT;
            DrawTextEx(font, e->username, (Vector2){tx, ey + (FRIEND_ENTRY_HEIGHT - FONT_SIZE) / 2.0f},
                       FONT_SIZE, FONT_SPACING, ncol);
            /* Invite button */
            if (state->can_invite && e->presence == FRIEND_PRESENCE_ONLINE) {
                Rectangle ir = {btn_x - BTN_W - 2, ey + 8, BTN_W, BTN_H};
                draw_button(font, "Invite", ir, COL_BTN_INVITE, COL_TEXT);
            }
        }
    }

    EndScissorMode();

    /* ---- Search bar ---- */
    {
        Rectangle sb = {pr.x + 2, search_top + 2, pr.width - 4, FRIEND_SEARCH_HEIGHT - 4};
        DrawRectangleRec(sb, COL_SEARCH_BG);
        if (state->search_active) {
            DrawRectangleLinesEx(sb, 1, COL_LABEL_INVITE);
        }

        const char *display = (state->search_len > 0) ? state->search_buf : "Search players...";
        Color tc = (state->search_len > 0) ? COL_TEXT : COL_TEXT_DIM;
        DrawTextEx(font, display, (Vector2){sb.x + 6, sb.y + (sb.height - FONT_SM) / 2.0f},
                   FONT_SM, FONT_SPACING, tc);
    }

    /* ---- Search results (above search bar) ---- */
    if (state->search_results_visible && state->search_result_count > 0) {
        int shown = 0;
        for (int i = 0; i < state->search_result_count; i++) {
            if (state->search_results[i].status == FRIEND_STATUS_SELF) continue;
            shown++;
        }
        float results_h = shown * 28.0f + 4;
        float ry = search_top - results_h;
        Rectangle rbg = {pr.x, ry, pr.width, results_h};
        DrawRectangleRec(rbg, COL_PANEL_BG);

        float cy = ry + 2;
        for (int i = 0; i < state->search_result_count; i++) {
            if (state->search_results[i].status == FRIEND_STATUS_SELF) continue;

            Rectangle row = {pr.x + 2, cy, pr.width - 4, 26};
            DrawRectangleRec(row, is_hovered(row) ? COL_ENTRY_HOVER : COL_ENTRY_BG);
            DrawTextEx(font, state->search_results[i].username,
                       (Vector2){row.x + 4, cy + 6}, FONT_SM, FONT_SPACING, COL_TEXT);

            Rectangle br = {row.x + row.width - BTN_W - 2, cy + 3, BTN_W, 20};
            uint8_t st = state->search_results[i].status;
            if (st == FRIEND_STATUS_AVAILABLE) {
                draw_button(font, "Add", br, COL_BTN_ADD, COL_TEXT);
            } else if (st == FRIEND_STATUS_ALREADY_FRIEND) {
                draw_button(font, "Friends", br, COL_BTN_BLOCKED, COL_TEXT_DIM);
            } else if (st == FRIEND_STATUS_PENDING_SENT || st == FRIEND_STATUS_PENDING_RECEIVED) {
                draw_button(font, "Pending", br, COL_BTN_BLOCKED, COL_TEXT_DIM);
            } else if (st == FRIEND_STATUS_BLOCKED) {
                draw_button(font, "Blocked", br, COL_BTN_BLOCKED, COL_TEXT_DIM);
            }
            cy += 28;
        }
    }

    /* ---- Context menu ---- */
    if (state->context_menu_open) {
        Rectangle cm = {state->context_menu_x, state->context_menu_y, 120, 28};
        DrawRectangleRec(cm, COL_CONTEXT_BG);
        DrawRectangleLinesEx(cm, 1, COL_TEXT_DIM);
        draw_button(font, "Remove Friend", cm, COL_CONTEXT_BG, COL_TEXT);
    }

    /* ---- Confirmation dialog ---- */
    if (state->confirm_remove_open && state->confirm_remove_entry >= 0 &&
        state->confirm_remove_entry < state->entry_count) {
        /* Dim overlay */
        DrawRectangleRec(pr, COL_OVERLAY);

        float dw = 180, dh = 80;
        Rectangle dlg = {pr.x + (pr.width - dw) / 2.0f,
                         pr.y + (pr.height - dh) / 2.0f, dw, dh};
        DrawRectangleRec(dlg, COL_CONFIRM_BG);
        DrawRectangleLinesEx(dlg, 1, COL_TEXT_DIM);

        char prompt[64];
        snprintf(prompt, sizeof(prompt), "Remove %s?",
                 state->entries[state->confirm_remove_entry].username);
        Vector2 psz = MeasureTextEx(font, prompt, FONT_SM, FONT_SPACING);
        DrawTextEx(font, prompt,
                   (Vector2){dlg.x + (dlg.width - psz.x) / 2.0f, dlg.y + 12},
                   FONT_SM, FONT_SPACING, COL_TEXT);

        Rectangle yes_r = {dlg.x + 20, dlg.y + dh - 30, 60, 22};
        Rectangle no_r = {dlg.x + dw - 80, dlg.y + dh - 30, 60, 22};
        draw_button(font, "Yes", yes_r, COL_BTN_REJECT, COL_TEXT);
        draw_button(font, "No", no_r, COL_BTN_ACCEPT, COL_TEXT);
    }
}

/* ================================================================
 * Input
 * ================================================================ */

void friend_panel_render_input(FriendPanelState *state, Rectangle pr)
{
    Vector2 mouse = GetMousePosition();
    bool in_panel = CheckCollisionPointRec(mouse, pr);

    float content_top = pr.y + TITLE_HEIGHT;
    float search_top = pr.y + pr.height - FRIEND_SEARCH_HEIGHT;

    /* ---- Confirmation dialog input (takes priority) ---- */
    if (state->confirm_remove_open) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            float dw = 180, dh = 80;
            Rectangle dlg = {pr.x + (pr.width - dw) / 2.0f,
                             pr.y + (pr.height - dh) / 2.0f, dw, dh};
            Rectangle yes_r = {dlg.x + 20, dlg.y + dh - 30, 60, 22};
            Rectangle no_r = {dlg.x + dw - 80, dlg.y + dh - 30, 60, 22};

            if (CheckCollisionPointRec(mouse, yes_r)) {
                int idx = state->confirm_remove_entry;
                if (idx >= 0 && idx < state->entry_count) {
                    lobby_client_friend_remove(state->entries[idx].account_id);
                    /* Remove from list */
                    for (int j = idx; j < state->entry_count - 1; j++)
                        state->entries[j] = state->entries[j + 1];
                    state->entry_count--;
                }
                state->confirm_remove_open = false;
            } else if (CheckCollisionPointRec(mouse, no_r)) {
                state->confirm_remove_open = false;
            }
        }
        return; /* Block other input while dialog is open */
    }

    /* ---- Context menu input ---- */
    if (state->context_menu_open) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Rectangle cm = {state->context_menu_x, state->context_menu_y, 120, 28};
            if (CheckCollisionPointRec(mouse, cm)) {
                state->confirm_remove_open = true;
                state->confirm_remove_entry = state->context_menu_entry;
            }
            state->context_menu_open = false;
        }
        return; /* Block other input while context menu is open */
    }

    /* ---- Scroll ---- */
    if (in_panel) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            state->scroll_target -= wheel * FRIEND_ENTRY_HEIGHT;
            float max_scroll = state->entry_count * FRIEND_ENTRY_HEIGHT - (search_top - content_top);
            if (max_scroll < 0) max_scroll = 0;
            if (state->scroll_target < 0) state->scroll_target = 0;
            if (state->scroll_target > max_scroll) state->scroll_target = max_scroll;
        }
    }

    /* ---- Search bar click / text input ---- */
    Rectangle sb = {pr.x + 2, search_top + 2, pr.width - 4, FRIEND_SEARCH_HEIGHT - 4};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(mouse, sb)) {
            state->search_active = true;
        } else {
            state->search_active = false;
            /* Close search results if clicking outside */
            if (state->search_results_visible) {
                int shown = 0;
                for (int i = 0; i < state->search_result_count; i++)
                    if (state->search_results[i].status != FRIEND_STATUS_SELF) shown++;
                float results_h = shown * 28.0f + 4;
                float ry = search_top - results_h;
                Rectangle rbg = {pr.x, ry, pr.width, results_h};
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
        /* Trigger search only when text changed and >= 4 chars */
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
        float results_h = 0;
        int shown = 0;
        for (int i = 0; i < state->search_result_count; i++)
            if (state->search_results[i].status != FRIEND_STATUS_SELF) shown++;
        results_h = shown * 28.0f + 4;
        float ry = search_top - results_h;
        float cy = ry + 2;
        for (int i = 0; i < state->search_result_count; i++) {
            if (state->search_results[i].status == FRIEND_STATUS_SELF) continue;
            Rectangle br = {pr.x + pr.width - BTN_W - 4, cy + 3, BTN_W, 20};
            if (state->search_results[i].status == FRIEND_STATUS_AVAILABLE &&
                CheckCollisionPointRec(mouse, br)) {
                lobby_client_friend_request(state->search_results[i].account_id);
                state->search_results[i].status = FRIEND_STATUS_PENDING_SENT;
            }
            cy += 28;
        }
    }

    /* ---- Entry button clicks ---- */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && in_panel) {
        for (int i = 0; i < state->entry_count; i++) {
            float ey = content_top + i * FRIEND_ENTRY_HEIGHT - state->scroll_offset;
            if (ey + FRIEND_ENTRY_HEIGHT < content_top || ey > search_top) continue;

            Rectangle er = {pr.x + 2, ey, pr.width - 4, FRIEND_ENTRY_HEIGHT - 2};
            if (!CheckCollisionPointRec(mouse, er)) continue;

            float btn_x = er.x + er.width;

            if (state->entries[i].type == FRIEND_ENTRY_INVITE ||
                state->entries[i].type == FRIEND_ENTRY_REQUEST) {
                Rectangle ar = {btn_x - BTN_W * 2 - 6, ey + 8, BTN_W, BTN_H};
                Rectangle rr = {btn_x - BTN_W - 2, ey + 8, BTN_W, BTN_H};

                if (CheckCollisionPointRec(mouse, ar)) {
                    /* Accept */
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
                    /* Reject */
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
                Rectangle ir = {btn_x - BTN_W - 2, ey + 8, BTN_W, BTN_H};
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
            float ey = content_top + i * FRIEND_ENTRY_HEIGHT - state->scroll_offset;
            if (ey + FRIEND_ENTRY_HEIGHT < content_top || ey > search_top) continue;
            Rectangle er = {pr.x + 2, ey, pr.width - 4, FRIEND_ENTRY_HEIGHT - 2};
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
