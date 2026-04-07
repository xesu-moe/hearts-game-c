#ifndef HELP_MENU_H
#define HELP_MENU_H

/* ============================================================
 * @deps-exports: HelpMenuNode, HelpMenuState,
 *                help_menu_load(), help_menu_update(),
 *                help_menu_draw(), help_menu_draw_tooltip()
 * @deps-requires: raylib.h
 * @deps-used-by: render.h (HelpMenuState field), render.c (init/update/draw)
 * @deps-last-changed: 2026-04-05 — Created
 * ============================================================ */

#include <stdbool.h>
#include "raylib.h"

/* Forward declaration */
struct RenderState;

#define HELP_MENU_MAX_NODES    80
#define HELP_MENU_MAX_LABEL    48
#define HELP_MENU_MAX_DESC     512
#define HELP_MENU_MAX_CHILDREN 16
#define HELP_MENU_MAX_DEPTH    4

typedef struct HelpMenuNode {
    char label[HELP_MENU_MAX_LABEL];
    char description[HELP_MENU_MAX_DESC];   /* non-empty = leaf node */
    int  parent;                             /* -1 for root-level */
    int  children[HELP_MENU_MAX_CHILDREN];
    int  child_count;
    int  depth;                              /* 0 = top category, 1 = sub-item, etc. */
    int  sprite_id;                          /* transmutation sprite, -1 = none */
} HelpMenuNode;

typedef struct HelpMenuState {
    /* Tree data (loaded once from JSON) */
    HelpMenuNode nodes[HELP_MENU_MAX_NODES];
    int          node_count;
    bool         loaded;

    /* Runtime UI state */
    int          open_path[HELP_MENU_MAX_DEPTH]; /* expanded node per depth, -1 = none */
    int          open_depth;                      /* how many cascade levels open (0=closed) */
    int          hover_node;                      /* currently hovered node, -1 = none */
    Rectangle    button_rect;                     /* "? Help" button bounds */
    Rectangle    column_rects[HELP_MENU_MAX_DEPTH]; /* bounding rect per cascade column */
    Rectangle    forgiving_zone;                  /* union of all rects + padding */

    /* Tooltip animation */
    float        tooltip_anim_t;   /* 0->1 grow-in, 1->0 shrink-out */
    int          tooltip_node;     /* leaf node driving anim direction, -1 = none */
    int          tooltip_show_node; /* last valid node for drawing during shrink-out */
} HelpMenuState;

/* Load help menu structure from a JSON file.
 * |mode| selects the top-level key (e.g. "hollow_hearts").
 * Returns true on success, false on error. */
bool help_menu_load(HelpMenuState *hm, const char *path, const char *mode);

/* Per-frame update: hover detection, cascade management, tooltip animation.
 * pause_active should be true when the pause overlay is shown. */
void help_menu_update(HelpMenuState *hm, const struct RenderState *rs,
                      float dt, bool pause_active);

/* Draw the help button and any open cascade columns. */
void help_menu_draw(const HelpMenuState *hm, const struct RenderState *rs);

/* Draw the tooltip description window (call in overlay pass). */
void help_menu_draw_tooltip(const HelpMenuState *hm,
                            const struct RenderState *rs);

#endif /* HELP_MENU_H */
