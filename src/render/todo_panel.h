/* ============================================================
 * @deps-exports: struct TodoPanelState, TODO_PANEL_WIDTH, TODO_MAX_ITEMS, TODO_MAX_ITEM_LEN, todo_panel_init(), todo_panel_height(), todo_panel_draw()
 * @deps-requires: raylib.h (Rectangle, Font)
 * @deps-used-by: render.h, render.c, main.c
 * ============================================================ */
#ifndef TODO_PANEL_H
#define TODO_PANEL_H

#include <raylib.h>
#include <stdbool.h>

struct RenderState;

#define TODO_PANEL_WIDTH    520
#define TODO_MAX_ITEMS       32
#define TODO_MAX_ITEM_LEN   128

typedef struct {
    char known_bugs[TODO_MAX_ITEMS][TODO_MAX_ITEM_LEN];
    int  known_bugs_count;

    char testing_required[TODO_MAX_ITEMS][TODO_MAX_ITEM_LEN];
    int  testing_required_count;

    char planned_features[TODO_MAX_ITEMS][TODO_MAX_ITEM_LEN];
    int  planned_features_count;

    bool loaded;
} TodoPanelState;

/* Load assets/defs/todo.json and populate state. Call once at startup. */
void todo_panel_init(TodoPanelState *state);

/* Calculate required panel height based on loaded content.
   scale: rs->layout.scale / 1.5f (1.0 at 1080p). */
float todo_panel_height(const TodoPanelState *state, float scale);

/* Draw the panel at the given rectangle. */
void todo_panel_draw(const TodoPanelState *state, Rectangle panel_rect,
                     const struct RenderState *rs);

#endif /* TODO_PANEL_H */
