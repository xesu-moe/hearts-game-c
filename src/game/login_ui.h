/* ============================================================
 * Login UI — State and Text Input
 *
 * Manages the login screen state: username text input buffer,
 * status/error display, and cursor blink timer.
 *
 * @deps-exports: LoginUIState, login_ui_init, login_ui_update_text_input
 * @deps-requires: net/protocol.h (NET_MAX_NAME_LEN)
 * @deps-used-by: main.c, game/update.c, render/render.c
 * @deps-last-changed: 2026-03-25 — Step 19: Login & Register UI
 * ============================================================ */

#ifndef LOGIN_UI_H
#define LOGIN_UI_H

#include <stdbool.h>

#include "net/protocol.h"

typedef struct LoginUIState {
    char  username_buf[NET_MAX_NAME_LEN];
    int   username_len;
    float cursor_blink;          /* 0..1 oscillating timer */
    bool  show_username_input;   /* true = first launch, show text field */
    bool  has_stored_username;   /* true = auto-login path */
    char  status_text[128];      /* "Connecting...", "Logging in..." */
    char  error_text[128];       /* error message to display */
    bool  awaiting_response;     /* true while waiting for server */
} LoginUIState;

/* Initialize login UI state to defaults. */
void login_ui_init(LoginUIState *lui);

/* Poll Raylib text input and update username_buf.
 * Call once per frame when show_username_input is true. */
void login_ui_update_text_input(LoginUIState *lui, float dt);

#endif /* LOGIN_UI_H */
