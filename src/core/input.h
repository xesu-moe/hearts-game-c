/* ============================================================
 * Raylib input polling and button state tracking.
 * Client-only — the server uses input_cmd.h directly.
 *
 * @deps-exports: InputAction, InputState,
 *                input_init/poll/get_state()
 * @deps-requires: input_cmd.h (InputCmdType, InputCmd, queue API),
 *                 raylib.h (Vector2)
 * @deps-used-by: input.c, process_input.c, main.c
 * @deps-last-changed: 2026-03-22 — Split command types to input_cmd.h
 * ============================================================ */

#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

#include "raylib.h"

#include "input_cmd.h"

/* ---- Button state tracking ---- */

typedef enum InputAction {
    INPUT_ACTION_CONFIRM = 0,
    INPUT_ACTION_CANCEL,
    INPUT_ACTION_LEFT_CLICK,
    INPUT_ACTION_RIGHT_CLICK,
    INPUT_ACTION_COUNT
} InputAction;

typedef struct InputState {
    bool    pressed[INPUT_ACTION_COUNT];
    bool    held[INPUT_ACTION_COUNT];
    bool    released[INPUT_ACTION_COUNT];
    Vector2 mouse_pos;
} InputState;

/* ---- Public API ---- */

void input_init(void);
void input_poll(void);
const InputState *input_get_state(void);

#endif /* INPUT_H */
