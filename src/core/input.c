/* ============================================================
 * @deps-implements: input.h
 * @deps-requires: input.h, input_cmd.h, raylib.h
 * @deps-last-changed: 2026-03-22 — Queue code moved to input_cmd.c
 * ============================================================ */

#include "input.h"

/* ---- File-static global ---- */

static InputState g_input_state;

/* ---- Implementation ---- */

void input_init(void)
{
    g_input_state = (InputState){0};
}

void input_poll(void)
{
    g_input_state.pressed[INPUT_ACTION_CONFIRM]     = IsKeyPressed(KEY_ENTER);
    g_input_state.pressed[INPUT_ACTION_CANCEL]      = IsKeyPressed(KEY_ESCAPE);
    g_input_state.pressed[INPUT_ACTION_LEFT_CLICK]  = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    g_input_state.pressed[INPUT_ACTION_RIGHT_CLICK] = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);

    g_input_state.held[INPUT_ACTION_CONFIRM]     = IsKeyDown(KEY_ENTER);
    g_input_state.held[INPUT_ACTION_CANCEL]      = IsKeyDown(KEY_ESCAPE);
    g_input_state.held[INPUT_ACTION_LEFT_CLICK]  = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    g_input_state.held[INPUT_ACTION_RIGHT_CLICK] = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

    g_input_state.released[INPUT_ACTION_CONFIRM]     = IsKeyReleased(KEY_ENTER);
    g_input_state.released[INPUT_ACTION_CANCEL]      = IsKeyReleased(KEY_ESCAPE);
    g_input_state.released[INPUT_ACTION_LEFT_CLICK]  = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    g_input_state.released[INPUT_ACTION_RIGHT_CLICK] = IsMouseButtonReleased(MOUSE_BUTTON_RIGHT);

    g_input_state.mouse_pos = GetMousePosition();

    if (g_input_state.pressed[INPUT_ACTION_CONFIRM]) {
        input_cmd_push((InputCmd){
            .type          = INPUT_CMD_CONFIRM,
            .source_player = 0,
        });
    }
    if (g_input_state.pressed[INPUT_ACTION_CANCEL]) {
        input_cmd_push((InputCmd){
            .type          = INPUT_CMD_CANCEL,
            .source_player = 0,
        });
    }
    if (g_input_state.pressed[INPUT_ACTION_LEFT_CLICK]) {
        input_cmd_push((InputCmd){
            .type          = INPUT_CMD_CLICK,
            .source_player = 0,
            .mouse_pos     = {g_input_state.mouse_pos.x,
                              g_input_state.mouse_pos.y},
        });
    }
}

const InputState *input_get_state(void)
{
    return &g_input_state;
}
