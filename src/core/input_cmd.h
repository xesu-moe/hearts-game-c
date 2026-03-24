#ifndef INPUT_CMD_H
#define INPUT_CMD_H

/* ============================================================
 * Raylib-free command types and queue for input commands.
 * Shared by client, server, and lobby.
 *
 * @deps-exports: InputCmdType, InputCmd, InputCmdQueue,
 *                INPUT_CMD_QUEUE_CAPACITY,
 *                input_cmd_push/pop/cmd_queue_empty/clear()
 * @deps-requires: card.h (Card)
 * @deps-used-by: input_cmd.c, input.h, protocol.c, update.c,
 *                process_input.c, main.c, server_game.c
 * @deps-last-changed: 2026-03-22 — Split from input.h for server compatibility
 * ============================================================ */

#include <stdbool.h>

#include "card.h"

/* ---- Command types ---- */

typedef enum InputCmdType {
    INPUT_CMD_NONE = 0,

    /* UI navigation */
    INPUT_CMD_CONFIRM,
    INPUT_CMD_CANCEL,

    /* Card interaction */
    INPUT_CMD_SELECT_CARD,
    INPUT_CMD_PLAY_CARD,

    /* Game flow */
    INPUT_CMD_START_GAME,
    INPUT_CMD_QUIT,

    /* Raw positional (for UI hit-testing) */
    INPUT_CMD_CLICK,

    /* Phase 2: Contract selection */
    INPUT_CMD_SELECT_CONTRACT,

    /* Phase 2: Transmutation */
    INPUT_CMD_SELECT_TRANSMUTATION,
    INPUT_CMD_APPLY_TRANSMUTATION,

    /* Settings */
    INPUT_CMD_OPEN_SETTINGS,
    INPUT_CMD_SETTING_PREV,
    INPUT_CMD_SETTING_NEXT,
    INPUT_CMD_APPLY_DISPLAY,

    /* Phase 2: Rogue reveal */
    INPUT_CMD_ROGUE_REVEAL,

    /* Phase 2: Duel swap */
    INPUT_CMD_DUEL_PICK,
    INPUT_CMD_DUEL_GIVE,
    INPUT_CMD_DUEL_RETURN,

    /* Dealer phase */
    INPUT_CMD_DEALER_DIR,
    INPUT_CMD_DEALER_AMT,
    INPUT_CMD_DEALER_CONFIRM,

    /* Pause menu */
    INPUT_CMD_RETURN_TO_MENU,

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
            int  card_index;
            Card card;
        } card;

        /* INPUT_CMD_CLICK: */
        struct {
            float x;
            float y;
        } mouse_pos;

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
        struct { int setting_id; } setting;

        /* INPUT_CMD_DEALER_DIR: */
        struct { int direction; } dealer_dir;

        /* INPUT_CMD_DEALER_AMT: */
        struct { int amount; } dealer_amt;
    };
} InputCmd;

/* ---- Command Queue (ring buffer, value-typed) ---- */

#define INPUT_CMD_QUEUE_CAPACITY 16

typedef struct InputCmdQueue {
    InputCmd cmds[INPUT_CMD_QUEUE_CAPACITY];
    int      head;
    int      tail;
    int      count;
} InputCmdQueue;

/* ---- Queue API ---- */

bool     input_cmd_push(InputCmd cmd);
InputCmd input_cmd_pop(void);
bool     input_cmd_queue_empty(void);
void     input_cmd_queue_clear(void);

#endif /* INPUT_CMD_H */
