/* ============================================================
 * @deps-implements: input.h
 * @deps-requires: input.h, raylib.h, card.h
 * @deps-last-changed: 2026-03-14 — Initial creation
 * ============================================================ */

#include "raylib.h"

#include "input.h"

/* ---- File-static globals ---- */

static InputCmdQueue g_cmd_queue;   /* the command ring buffer */
static InputState    g_input_state; /* current frame's polled state */

/* ---- Implementation ---- */

void input_init(void)
{
    /* Zero everything. Queue starts empty, all buttons unpressed. */
    g_cmd_queue   = (InputCmdQueue){0};
    g_input_state = (InputState){0};
}

void input_poll(void)
{
    /* Use Raylib's built-in edge detection -- it tracks press/release
     * transitions internally (updated once per PollInputEvents). This
     * avoids stale edge flags when update() runs multiple fixed-step
     * iterations per frame. */

    /* Edge-detected (pressed this frame) */
    g_input_state.pressed[INPUT_ACTION_CONFIRM]     = IsKeyPressed(KEY_ENTER);
    g_input_state.pressed[INPUT_ACTION_CANCEL]       = IsKeyPressed(KEY_ESCAPE);
    g_input_state.pressed[INPUT_ACTION_LEFT_CLICK]  = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    g_input_state.pressed[INPUT_ACTION_RIGHT_CLICK] = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);

    /* Held (currently down) */
    g_input_state.held[INPUT_ACTION_CONFIRM]     = IsKeyDown(KEY_ENTER);
    g_input_state.held[INPUT_ACTION_CANCEL]      = IsKeyDown(KEY_ESCAPE);
    g_input_state.held[INPUT_ACTION_LEFT_CLICK]  = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    g_input_state.held[INPUT_ACTION_RIGHT_CLICK] = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

    /* Released (just released this frame) */
    g_input_state.released[INPUT_ACTION_CONFIRM]     = IsKeyReleased(KEY_ENTER);
    g_input_state.released[INPUT_ACTION_CANCEL]      = IsKeyReleased(KEY_ESCAPE);
    g_input_state.released[INPUT_ACTION_LEFT_CLICK]  = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    g_input_state.released[INPUT_ACTION_RIGHT_CLICK] = IsMouseButtonReleased(MOUSE_BUTTON_RIGHT);

    /* Mouse position */
    g_input_state.mouse_pos = GetMousePosition();

    /* Generate unambiguous commands from keyboard/mouse */
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
            .mouse_pos     = g_input_state.mouse_pos,
        });
    }
}

bool input_cmd_push(InputCmd cmd)
{
    if (g_cmd_queue.count >= INPUT_CMD_QUEUE_CAPACITY) {
        /* Queue full -- drop command. In a card game this should never
         * happen under normal operation. */
        return false;
    }
    g_cmd_queue.cmds[g_cmd_queue.tail] = cmd; /* value copy */
    g_cmd_queue.tail = (g_cmd_queue.tail + 1) % INPUT_CMD_QUEUE_CAPACITY;
    g_cmd_queue.count++;
    return true;
}

InputCmd input_cmd_pop(void)
{
    if (g_cmd_queue.count == 0) {
        return (InputCmd){ .type = INPUT_CMD_NONE };
    }
    InputCmd cmd = g_cmd_queue.cmds[g_cmd_queue.head]; /* value copy */
    g_cmd_queue.head = (g_cmd_queue.head + 1) % INPUT_CMD_QUEUE_CAPACITY;
    g_cmd_queue.count--;
    return cmd;
}

bool input_cmd_queue_empty(void)
{
    return g_cmd_queue.count == 0;
}

void input_cmd_queue_clear(void)
{
    g_cmd_queue.head  = 0;
    g_cmd_queue.tail  = 0;
    g_cmd_queue.count = 0;
}

const InputState *input_get_state(void)
{
    return &g_input_state;
}
