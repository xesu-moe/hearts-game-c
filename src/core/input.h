/* ============================================================
 * @deps-exports: InputCmdType (SELECT_CARD, PLAY_CARD, DUEL_PICK, DUEL_GIVE,
 *                DUEL_RETURN, ROGUE_REVEAL, RETURN_TO_MENU, etc),
 *                InputCmd (rogue_reveal, duel_pick, duel_give union members),
 *                InputCmdQueue, InputAction, InputState,
 *                input_init/poll/cmd_push/pop/cmd_queue_empty/clear/get_state()
 * @deps-requires: raylib.h (Vector2), card.h (Card)
 * @deps-used-by: input.c, process_input.c, update.c, main.c
 * @deps-last-changed: 2026-03-20 — Added INPUT_CMD_DUEL_* commands and unions
 * ============================================================ */

#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

#include "raylib.h"

#include "card.h"

/* ---- Command types ---- */

/* Command types for vanilla Hearts + Phase 2 mechanics. */
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

    /* Phase 2: Transmutation */
    INPUT_CMD_SELECT_TRANSMUTATION,  /* Player clicked a transmutation inventory button */
    INPUT_CMD_APPLY_TRANSMUTATION,   /* Player clicked a hand card to apply selected transmutation */

    /* Settings */
    INPUT_CMD_OPEN_SETTINGS,
    INPUT_CMD_SETTING_PREV,          /* navigate setting left */
    INPUT_CMD_SETTING_NEXT,          /* navigate setting right */
    INPUT_CMD_APPLY_DISPLAY,         /* apply display settings (window/res/fps) */

    /* Phase 2: Rogue reveal */
    INPUT_CMD_ROGUE_REVEAL,          /* reveal an opponent's card (Rogue effect) */

    /* Phase 2: Duel swap */
    INPUT_CMD_DUEL_PICK,             /* pick an opponent's card (Duel step 1) */
    INPUT_CMD_DUEL_GIVE,             /* pick own card to give (Duel step 2) */
    INPUT_CMD_DUEL_RETURN,           /* return opponent's card (cancel swap) */

    /* Dealer phase */
    INPUT_CMD_DEALER_DIR,            /* select pass direction */
    INPUT_CMD_DEALER_AMT,            /* select pass amount */
    INPUT_CMD_DEALER_CONFIRM,        /* confirm dealer choices */

    /* Pause menu */
    INPUT_CMD_RETURN_TO_MENU,        /* return to main menu from pause */

    INPUT_CMD_COUNT
} InputCmdType;

/* Command payload -- tagged union. Each command type documents which
 * union field it uses. Fields not listed are unused/zero. */
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

        /* INPUT_CMD_SELECT_TRANSMUTATION: */
        struct { int inv_slot; } transmute_select;

        /* INPUT_CMD_APPLY_TRANSMUTATION: */
        struct { int hand_index; } transmute_apply;

        /* INPUT_CMD_ROGUE_REVEAL: */
        struct { int target_player; int hand_index; } rogue_reveal;

        /* INPUT_CMD_DUEL_PICK: */
        struct { int target_player; int hand_index; } duel_pick;

        /* INPUT_CMD_DUEL_GIVE: */
        struct { int hand_index; } duel_give;

        /* INPUT_CMD_SETTING_PREV, INPUT_CMD_SETTING_NEXT: */
        struct { int setting_id; } setting; /* which setting row (0-based) */

        /* INPUT_CMD_DEALER_DIR: */
        struct { int direction; } dealer_dir; /* 0=left, 1=across, 2=right */

        /* INPUT_CMD_DEALER_AMT: */
        struct { int amount; } dealer_amt; /* 0, 2, 3, or 4 */
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
