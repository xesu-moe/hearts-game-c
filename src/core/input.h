/* ============================================================
 * @deps-exports: InputCmdType (SELECT_CONTRACT, SELECT_GRUDGE_REVENGE,
 *                GRUDGE_DISCARD_CHOICE, SKIP_GRUDGE, OPEN_SETTINGS, etc),
 *                InputCmd (contract, grudge_revenge, grudge_discard, setting),
 *                InputCmdQueue, InputAction, InputState, input_init(),
 *                input_poll(), input_cmd_push(), input_cmd_pop(),
 *                input_cmd_queue_empty(), input_cmd_queue_clear(),
 *                input_get_state(), INPUT_CMD_QUEUE_CAPACITY
 * @deps-requires: raylib.h, card.h (Card, Vector2)
 * @deps-used-by: main.c
 * @deps-last-changed: 2026-03-17 — Removed INPUT_CMD_ACTIVATE_GRUDGE (modal UI replaced)
 * ============================================================ */

#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

#include "raylib.h"

#include "card.h"

/* ---- Command types ---- */

/* Phase 1 (vanilla Hearts) command types only.
 * Phase 2 will add: SELECT_CONTRACT, SELECT_HOST_ACTION, ACTIVATE_REVENGE,
 * SELECT_CHARACTER. The enum and union are designed to extend cleanly. */
typedef enum InputCmdType {
    INPUT_CMD_NONE = 0,

    /* UI navigation */
    INPUT_CMD_CONFIRM,      /* confirm current selection (Enter/click button) */
    INPUT_CMD_CANCEL,       /* cancel/back (Escape) */

    /* Card interaction */
    INPUT_CMD_SELECT_CARD,  /* click a card to select/toggle it */
    INPUT_CMD_PLAY_CARD,    /* play the selected/clicked card */

    /* Game flow */
    INPUT_CMD_START_GAME,   /* start game from menu */
    INPUT_CMD_QUIT,         /* quit game */

    /* Raw positional (for UI hit-testing) */
    INPUT_CMD_CLICK,        /* mouse click with position */

    /* Phase 2: Contract selection */
    INPUT_CMD_SELECT_CONTRACT, /* select a contract during passing phase */

    /* Phase 2: Grudge token */
    INPUT_CMD_SELECT_GRUDGE_REVENGE, /* select which revenge to apply */
    INPUT_CMD_GRUDGE_DISCARD_CHOICE, /* choose old vs new token when conflict */
    INPUT_CMD_SKIP_GRUDGE,           /* decline to use grudge this turn */

    /* Settings */
    INPUT_CMD_OPEN_SETTINGS,
    INPUT_CMD_SETTING_PREV,          /* navigate setting left */
    INPUT_CMD_SETTING_NEXT,          /* navigate setting right */
    INPUT_CMD_APPLY_DISPLAY,         /* apply display settings (window/res/fps) */

    INPUT_CMD_COUNT
} InputCmdType;

/* Command payload -- tagged union. Each command type documents which
 * union field it uses. Fields not listed are unused/zero.
 *
 * Phase 2 additions will add new union members (contract_id, host_action_id,
 * revenge struct) without changing existing fields. */
typedef struct InputCmd {
    InputCmdType type;
    int          source_player; /* who issued this command (0 = human, 1-3 = AI) */

    union {
        /* INPUT_CMD_SELECT_CARD, INPUT_CMD_PLAY_CARD: */
        struct {
            int  card_index; /* index within the player's hand */
            Card card;       /* the actual card (redundant but convenient) */
        } card;

        /* INPUT_CMD_CLICK: */
        Vector2 mouse_pos;

        /* INPUT_CMD_SELECT_CONTRACT: */
        struct { int contract_id; } contract;

        /* INPUT_CMD_SELECT_GRUDGE_REVENGE: */
        struct { int revenge_id; } grudge_revenge;

        /* INPUT_CMD_GRUDGE_DISCARD_CHOICE: */
        struct { int keep_new; } grudge_discard; /* 0=keep old, 1=keep new */

        /* INPUT_CMD_SETTING_PREV, INPUT_CMD_SETTING_NEXT: */
        struct { int setting_id; } setting; /* which setting row (0-based) */
    };
} InputCmd;

/* ---- Command Queue (ring buffer, value-typed) ---- */

#define INPUT_CMD_QUEUE_CAPACITY 16

typedef struct InputCmdQueue {
    InputCmd cmds[INPUT_CMD_QUEUE_CAPACITY];
    int      head;  /* index of next command to dequeue */
    int      tail;  /* index of next free slot */
    int      count; /* number of commands in queue */
} InputCmdQueue;

/* ---- Button state tracking ---- */

/* Tracks edge-detected state for keyboard/mouse actions relevant to Hearts.
 * This is the "action mapping" layer, adapted for a mouse-driven card game
 * (no analog sticks, no movement axes). */
typedef enum InputAction {
    INPUT_ACTION_CONFIRM = 0, /* Enter key or left-click on confirm button */
    INPUT_ACTION_CANCEL,      /* Escape key */
    INPUT_ACTION_LEFT_CLICK,  /* Mouse left button */
    INPUT_ACTION_RIGHT_CLICK, /* Mouse right button (deselect/cancel) */
    INPUT_ACTION_COUNT
} InputAction;

typedef struct InputState {
    bool    pressed[INPUT_ACTION_COUNT];  /* just pressed this frame */
    bool    held[INPUT_ACTION_COUNT];     /* currently held */
    bool    released[INPUT_ACTION_COUNT]; /* just released this frame */
    Vector2 mouse_pos;                    /* current mouse position */
} InputState;

/* ---- Public API ---- */

/* Initialize input system. Call once at startup, after InitWindow(). */
void input_init(void);

/* Poll Raylib for raw input, update InputState, generate commands
 * for unambiguous inputs (e.g., Escape -> CANCEL, Enter -> CONFIRM).
 * Does NOT generate card selection commands -- those require game state
 * context (hit-testing against card positions) and are generated by the
 * game layer after consulting InputState. */
void input_poll(void);

/* Push a command into the queue. Used by:
 * - input_poll() for keyboard-driven commands
 * - Game layer for mouse-driven commands (after hit-testing card positions)
 * - AI system for AI player commands
 * Returns false if queue is full (drops command with warning). */
bool input_cmd_push(InputCmd cmd);

/* Pop the next command from the queue.
 * Returns a command with type INPUT_CMD_NONE if queue is empty. */
InputCmd input_cmd_pop(void);

/* Check if queue has commands waiting. */
bool input_cmd_queue_empty(void);

/* Clear all pending commands. Called at phase transitions to prevent
 * stale commands from leaking across phases. */
void input_cmd_queue_clear(void);

/* Read-only access to current frame's input state (for game layer hit-testing). */
const InputState *input_get_state(void);

#endif /* INPUT_H */
