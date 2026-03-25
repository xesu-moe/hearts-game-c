/* ============================================================
 * @deps-implements: game/online_ui.h
 * @deps-requires: game/online_ui.h (OnlineUIState),
 *                 raylib.h (GetCharPressed, IsKeyPressed,
 *                 IsKeyPressedRepeat, KEY_BACKSPACE),
 *                 stdio.h, string.h
 * @deps-last-changed: 2026-03-25 — Step 20: Online Menu & Room UI
 * ============================================================ */

#include "online_ui.h"

#include <stdio.h>
#include <string.h>

#include <raylib.h>

/* Room codes use uppercase alphanumeric, excluding ambiguous chars (O/0/I/l).
 * Accept lowercase too and convert to uppercase. */
static bool is_valid_room_code_char(int ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '2' && ch <= '9');  /* exclude 0 and 1 */
}

static char to_upper(char c)
{
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

void online_ui_init(OnlineUIState *oui)
{
    memset(oui, 0, sizeof(*oui));
    oui->subphase = ONLINE_SUB_MENU;
    snprintf(oui->status_text, sizeof(oui->status_text), "Online");
}

void online_ui_update_text_input(OnlineUIState *oui, float dt)
{
    /* Cursor blink */
    oui->cursor_blink += dt * 2.0f;
    if (oui->cursor_blink > 1.0f)
        oui->cursor_blink -= 1.0f;

    /* Room codes are 4 chars max */
    int max_len = NET_ROOM_CODE_LEN - 1; /* 4 chars + NUL = typically 7, but codes are 4 */
    if (max_len > 4) max_len = 4;

    /* Character input */
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        if (is_valid_room_code_char(ch) && oui->room_code_len < max_len) {
            oui->room_code_buf[oui->room_code_len++] = to_upper((char)ch);
            oui->room_code_buf[oui->room_code_len] = '\0';
        }
    }

    /* Backspace */
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        if (oui->room_code_len > 0) {
            oui->room_code_buf[--oui->room_code_len] = '\0';
        }
    }
}
