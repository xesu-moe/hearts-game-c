/* ============================================================
 * @deps-implements: input_cmd.h
 * @deps-requires: input_cmd.h
 * @deps-last-changed: 2026-03-22 — Split from input.c for server compatibility
 * ============================================================ */

#include "input_cmd.h"

/* ---- File-static global ---- */

static InputCmdQueue g_cmd_queue;

/* ---- Implementation ---- */

bool input_cmd_push(InputCmd cmd)
{
    if (g_cmd_queue.count >= INPUT_CMD_QUEUE_CAPACITY) {
        return false;
    }
    g_cmd_queue.cmds[g_cmd_queue.tail] = cmd;
    g_cmd_queue.tail = (g_cmd_queue.tail + 1) % INPUT_CMD_QUEUE_CAPACITY;
    g_cmd_queue.count++;
    return true;
}

InputCmd input_cmd_pop(void)
{
    if (g_cmd_queue.count == 0) {
        return (InputCmd){.type = INPUT_CMD_NONE};
    }
    InputCmd cmd = g_cmd_queue.cmds[g_cmd_queue.head];
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
    g_cmd_queue.head = 0;
    g_cmd_queue.tail = 0;
    g_cmd_queue.count = 0;
}
