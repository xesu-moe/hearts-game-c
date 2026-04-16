/* ============================================================
 * @deps-implements: game/login_ui.h
 * @deps-requires: game/login_ui.h (LoginUIState),
 *                 raylib.h (GetCharPressed, IsKeyPressed, IsKeyDown,
 *                 KEY_BACKSPACE, GetFrameTime),
 *                 string.h
 * @deps-last-changed: 2026-03-25 — Step 19: Login & Register UI
 * ============================================================ */

#include "login_ui.h"

#include <stdio.h>
#include <string.h>

#include <raylib.h>

/* Max username length (excluding NUL) — matches server validation */
#define USERNAME_MAX_CHARS 31

/* Characters allowed in usernames: [a-zA-Z0-9_] */
static bool is_valid_username_char(int ch)
{
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_';
}

void login_ui_init(LoginUIState *lui)
{
    memset(lui, 0, sizeof(*lui));
    snprintf(lui->status_text, sizeof(lui->status_text), "Connecting...");
}

void login_ui_update_text_input(LoginUIState *lui, float dt)
{
    /* Cursor blink */
    lui->cursor_blink += dt * 2.0f;
    if (lui->cursor_blink > 1.0f)
        lui->cursor_blink -= 1.0f;

    /* Don't accept input while waiting for server response */
    if (lui->awaiting_response) return;

    /* Character input */
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        if (is_valid_username_char(ch) && lui->username_len < USERNAME_MAX_CHARS) {
            lui->username_buf[lui->username_len++] = (char)ch;
            lui->username_buf[lui->username_len] = '\0';
        }
    }

    /* Backspace with key repeat */
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        if (lui->username_len > 0) {
            lui->username_buf[--lui->username_len] = '\0';
        }
    }
}
