/* ============================================================
 * @deps-implements: help_menu.h
 * @deps-requires: help_menu.h, render.h, easing.h, vendor/cJSON.h, raylib.h
 * @deps-last-changed: 2026-04-05 — Created
 * ============================================================ */

#include "help_menu.h"
#include "render.h"
#include "easing.h"
#include "card_render.h"
#include "vendor/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Constants ---- */

#define TOOLTIP_ANIM_SECS 0.15f
#define FORGIVING_PAD     8.0f   /* extra pixels (pre-scale) around menu */
#define ITEM_HEIGHT_REF   22.0f
#define BUTTON_W_REF      80.0f
#define BUTTON_H_REF      24.0f
#define COLUMN_PAD_REF    6.0f
#define COLUMN_GAP_REF    4.0f
#define TOOLTIP_MAX_W_REF 240.0f
#define INNER_PAD_REF     8.0f

/* ---- File reading (duplicated from json_parse.c to avoid exposing static) ---- */

static char *read_file_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

/* ---- JSON loading ---- */

/* Check if a JSON object is a leaf with description + optional sprite_id,
 * as opposed to a category with child nodes. Leaf objects always have a
 * "description" string key. */
static bool is_leaf_object(const cJSON *obj)
{
    return cJSON_IsObject(obj) && cJSON_GetObjectItem(obj, "description");
}

static int load_node_recursive(HelpMenuState *hm, cJSON *obj,
                               int parent, int depth)
{
    if (depth >= HELP_MENU_MAX_DEPTH) return 0;

    cJSON *child = NULL;
    int added = 0;
    cJSON_ArrayForEach(child, obj) {
        if (hm->node_count >= HELP_MENU_MAX_NODES) break;

        int idx = hm->node_count++;
        HelpMenuNode *node = &hm->nodes[idx];
        memset(node, 0, sizeof(*node));
        node->parent = parent;
        node->depth = depth;
        node->child_count = 0;
        node->sprite_id = -1;

        /* Copy label from JSON key */
        const char *key = child->string;
        if (key) {
            strncpy(node->label, key, HELP_MENU_MAX_LABEL - 1);
            node->label[HELP_MENU_MAX_LABEL - 1] = '\0';
        }

        if (cJSON_IsString(child)) {
            /* Leaf node — plain string description */
            const char *desc = cJSON_GetStringValue(child);
            if (desc) {
                strncpy(node->description, desc, HELP_MENU_MAX_DESC - 1);
                node->description[HELP_MENU_MAX_DESC - 1] = '\0';
            }
        } else if (is_leaf_object(child)) {
            /* Leaf node — object with description + optional sprite_id */
            const char *desc = cJSON_GetStringValue(
                cJSON_GetObjectItem(child, "description"));
            if (desc) {
                strncpy(node->description, desc, HELP_MENU_MAX_DESC - 1);
                node->description[HELP_MENU_MAX_DESC - 1] = '\0';
            }
            cJSON *sid = cJSON_GetObjectItem(child, "sprite_id");
            if (sid && cJSON_IsNumber(sid))
                node->sprite_id = sid->valueint;
        } else if (cJSON_IsObject(child)) {
            /* Category node — recurse into children */
            node->description[0] = '\0';
            load_node_recursive(hm, child, idx, depth + 1);
        }

        /* Register this node as a child of its parent */
        if (parent >= 0) {
            HelpMenuNode *p = &hm->nodes[parent];
            if (p->child_count < HELP_MENU_MAX_CHILDREN) {
                p->children[p->child_count++] = idx;
            }
        }
        added++;
    }
    return added;
}

bool help_menu_load(HelpMenuState *hm, const char *path, const char *mode)
{
    memset(hm, 0, sizeof(*hm));
    hm->tooltip_node = -1;
    hm->tooltip_show_node = -1;
    hm->hover_node = -1;
    for (int i = 0; i < HELP_MENU_MAX_DEPTH; i++)
        hm->open_path[i] = -1;

    char *text = read_file_text(path);
    if (!text) {
        fprintf(stderr, "[HELP] Could not load file \"%s\"\n", path);
        return false;
    }

    cJSON *root = cJSON_Parse(text);
    if (!root) {
        fprintf(stderr, "[HELP] Parse error in \"%s\"\n", path);
        free(text);
        return false;
    }

    /* Select the game mode subtree */
    cJSON *mode_root = cJSON_GetObjectItem(root, mode);
    if (!mode_root || !cJSON_IsObject(mode_root)) {
        fprintf(stderr, "[HELP] Mode \"%s\" not found in \"%s\"\n", mode, path);
        cJSON_Delete(root);
        free(text);
        return false;
    }

    /* Root-level children are top categories (depth 0) with parent = -1.
     * We create a virtual root array by iterating the mode object. */
    /* First, reserve indices for all root-level categories so they appear
     * contiguously. Then recurse into each. */
    int root_start = 0;
    int root_count = 0;
    cJSON *child = NULL;
    /* Count root items first */
    cJSON_ArrayForEach(child, mode_root) { root_count++; (void)child; }

    /* Pre-allocate root nodes */
    if (root_count > HELP_MENU_MAX_NODES) root_count = HELP_MENU_MAX_NODES;
    for (int i = 0; i < root_count; i++) {
        int idx = hm->node_count++;
        HelpMenuNode *node = &hm->nodes[idx];
        memset(node, 0, sizeof(*node));
        node->parent = -1;
        node->depth = 0;
        node->sprite_id = -1;
    }

    /* Fill root nodes and recurse into their children */
    int ri = 0;
    cJSON_ArrayForEach(child, mode_root) {
        if (ri >= root_count) break;
        int idx = root_start + ri;
        HelpMenuNode *node = &hm->nodes[idx];

        const char *key = child->string;
        if (key) {
            strncpy(node->label, key, HELP_MENU_MAX_LABEL - 1);
            node->label[HELP_MENU_MAX_LABEL - 1] = '\0';
        }

        if (cJSON_IsString(child)) {
            const char *desc = cJSON_GetStringValue(child);
            if (desc) {
                strncpy(node->description, desc, HELP_MENU_MAX_DESC - 1);
                node->description[HELP_MENU_MAX_DESC - 1] = '\0';
            }
        } else if (is_leaf_object(child)) {
            const char *desc = cJSON_GetStringValue(
                cJSON_GetObjectItem(child, "description"));
            if (desc) {
                strncpy(node->description, desc, HELP_MENU_MAX_DESC - 1);
                node->description[HELP_MENU_MAX_DESC - 1] = '\0';
            }
            cJSON *sid = cJSON_GetObjectItem(child, "sprite_id");
            if (sid && cJSON_IsNumber(sid))
                node->sprite_id = sid->valueint;
        } else if (cJSON_IsObject(child)) {
            node->description[0] = '\0';
            load_node_recursive(hm, child, idx, 1);
        }
        ri++;
    }

    cJSON_Delete(root);
    free(text);
    hm->loaded = true;
    fprintf(stderr, "[INFO] HELP: Loaded %d help menu nodes (mode: %s)\n",
            hm->node_count, mode);
    return true;
}

/* ---- Layout helpers ---- */

/* Compute the help button rect based on scoreboard HUD positioning. */
static Rectangle compute_button_rect(const LayoutConfig *cfg)
{
    float s = cfg->scale;
    float panel_w = (cfg->screen_width - cfg->board_size) * 0.5f;
    float margin = 4.0f * s;
    float pad = 10.0f * s;
    float x = panel_w + margin + pad;
    float y = margin + pad;

    /* Replicate scoreboard bg_h calculation */
    int font_header = (int)(16.0f * s);
    float row_h = 22.0f * s;
    float scoreboard_bg_h = row_h * 4 + (float)font_header + 4.0f * s + pad * 2;

    float btn_x = x - pad;  /* align with scoreboard background left edge */
    float btn_y = y - pad + scoreboard_bg_h + margin;
    float btn_w = BUTTON_W_REF * s;
    float btn_h = BUTTON_H_REF * s;

    return (Rectangle){btn_x, btn_y, btn_w, btn_h};
}

/* Measure the width needed for a column of node children. */
static float measure_column_width(const HelpMenuState *hm,
                                  const RenderState *rs,
                                  const int *child_indices, int count)
{
    float s = rs->layout.scale;
    int font_size = (int)(14.0f * s);
    float pad = COLUMN_PAD_REF * s;
    float max_w = 0.0f;

    for (int i = 0; i < count; i++) {
        const HelpMenuNode *node = &hm->nodes[child_indices[i]];
        float w = (float)hh_measure_text(rs, node->label, font_size);
        /* Add arrow indicator width for non-leaf nodes */
        if (node->child_count > 0) {
            w += (float)hh_measure_text(rs, " >", font_size);
        }
        if (w > max_w) max_w = w;
    }
    return max_w + pad * 2;
}

/* ---- Update ---- */

void help_menu_update(HelpMenuState *hm, const RenderState *rs,
                      float dt, bool pause_active)
{
    if (!hm->loaded) return;

    /* Suppress when paused */
    if (pause_active) {
        hm->open_depth = 0;
        hm->hover_node = -1;
        for (int i = 0; i < HELP_MENU_MAX_DEPTH; i++)
            hm->open_path[i] = -1;
        /* Let tooltip shrink out */
        if (hm->tooltip_anim_t > 0.0f) {
            hm->tooltip_anim_t -= (1.0f / TOOLTIP_ANIM_SECS) * dt;
            if (hm->tooltip_anim_t < 0.0f) {
                hm->tooltip_anim_t = 0.0f;
                hm->tooltip_node = -1;
                hm->tooltip_show_node = -1;
            }
        }
        return;
    }

    const LayoutConfig *cfg = &rs->layout;
    float s = cfg->scale;
    Vector2 mouse = GetMousePosition();

    /* Compute button rect */
    hm->button_rect = compute_button_rect(cfg);

    if (hm->open_depth == 0) {
        /* Menu is closed — check if mouse is over the button */
        if (CheckCollisionPointRec(mouse, hm->button_rect)) {
            hm->open_depth = 1;
            /* open_path[0] stays -1 (no category selected yet) */
        }
    }

    if (hm->open_depth > 0) {
        /* Build column rects and the forgiving zone */
        float item_h = ITEM_HEIGHT_REF * s;
        float col_gap = COLUMN_GAP_REF * s;
        float fpad = FORGIVING_PAD * s;

        /* Column 0: root categories, anchored to button right */
        int root_count = 0;
        int root_indices[HELP_MENU_MAX_CHILDREN];
        for (int i = 0; i < hm->node_count; i++) {
            if (hm->nodes[i].parent == -1 && root_count < HELP_MENU_MAX_CHILDREN) {
                root_indices[root_count++] = i;
            }
        }

        float col0_w = measure_column_width(hm, rs, root_indices, root_count);
        float col0_x = hm->button_rect.x + hm->button_rect.width + col_gap;
        float col0_y = hm->button_rect.y;
        float col0_h = item_h * root_count;
        hm->column_rects[0] = (Rectangle){col0_x, col0_y, col0_w, col0_h};

        /* Build deeper columns based on open_path */
        int active_columns = 1;
        for (int d = 0; d < HELP_MENU_MAX_DEPTH - 1; d++) {
            int parent_idx = hm->open_path[d];
            if (parent_idx < 0) break;
            const HelpMenuNode *pn = &hm->nodes[parent_idx];
            if (pn->child_count == 0) break;

            float prev_x = hm->column_rects[d].x;
            float prev_w = hm->column_rects[d].width;

            /* Find which item in column d is the parent — use its y for alignment */
            float parent_item_y = hm->column_rects[d].y; /* default */

            /* Determine the item index of open_path[d] within its parent's children (or root list) */
            if (d == 0) {
                for (int i = 0; i < root_count; i++) {
                    if (root_indices[i] == parent_idx) {
                        parent_item_y = col0_y + item_h * i;
                        break;
                    }
                }
            } else {
                int gp = hm->nodes[parent_idx].parent;
                if (gp >= 0) {
                    const HelpMenuNode *gpn = &hm->nodes[gp];
                    for (int i = 0; i < gpn->child_count; i++) {
                        if (gpn->children[i] == parent_idx) {
                            parent_item_y = hm->column_rects[d].y + item_h * i;
                            break;
                        }
                    }
                }
            }

            float col_w = measure_column_width(hm, rs, pn->children, pn->child_count);
            float col_x = prev_x + prev_w + col_gap;
            float col_y = parent_item_y; /* align with parent item */
            float col_h = item_h * pn->child_count;

            /* Screen boundary: flip leftward if needed */
            if (col_x + col_w > cfg->screen_width - 4.0f * s) {
                col_x = hm->column_rects[d].x - col_w - col_gap;
            }
            /* Clamp bottom */
            if (col_y + col_h > cfg->screen_height - 4.0f * s) {
                col_y = cfg->screen_height - 4.0f * s - col_h;
            }

            hm->column_rects[d + 1] = (Rectangle){col_x, col_y, col_w, col_h};
            active_columns++;
        }

        /* Compute forgiving zone: union of button + all active columns + padding */
        float min_x = hm->button_rect.x;
        float min_y = hm->button_rect.y;
        float max_x = hm->button_rect.x + hm->button_rect.width;
        float max_y = hm->button_rect.y + hm->button_rect.height;

        for (int i = 0; i < active_columns; i++) {
            Rectangle r = hm->column_rects[i];
            if (r.x < min_x) min_x = r.x;
            if (r.y < min_y) min_y = r.y;
            if (r.x + r.width > max_x) max_x = r.x + r.width;
            if (r.y + r.height > max_y) max_y = r.y + r.height;
        }
        hm->forgiving_zone = (Rectangle){
            min_x - fpad, min_y - fpad,
            (max_x - min_x) + fpad * 2,
            (max_y - min_y) + fpad * 2
        };

        /* Check if mouse is inside forgiving zone */
        if (!CheckCollisionPointRec(mouse, hm->forgiving_zone)) {
            /* Close menu */
            hm->open_depth = 0;
            hm->hover_node = -1;
            for (int i = 0; i < HELP_MENU_MAX_DEPTH; i++)
                hm->open_path[i] = -1;
        } else {
            /* Hit-test columns in reverse depth order (deepest first) */
            int new_hover = -1;
            int hover_depth = -1;

            /* Test column 0 (root categories) */
            if (CheckCollisionPointRec(mouse, hm->column_rects[0])) {
                int item = (int)((mouse.y - hm->column_rects[0].y) / item_h);
                if (item >= 0 && item < root_count) {
                    new_hover = root_indices[item];
                    hover_depth = 0;
                }
            }

            /* Test deeper columns */
            for (int d = 0; d < HELP_MENU_MAX_DEPTH - 1; d++) {
                int parent_idx = hm->open_path[d];
                if (parent_idx < 0) break;
                const HelpMenuNode *pn = &hm->nodes[parent_idx];
                if (pn->child_count == 0) break;

                Rectangle col = hm->column_rects[d + 1];
                if (CheckCollisionPointRec(mouse, col)) {
                    int item = (int)((mouse.y - col.y) / item_h);
                    if (item >= 0 && item < pn->child_count) {
                        new_hover = pn->children[item];
                        hover_depth = d + 1;
                    }
                }
            }

            hm->hover_node = new_hover;

            /* Update open_path based on hover */
            if (new_hover >= 0 && hover_depth >= 0) {
                const HelpMenuNode *hn = &hm->nodes[new_hover];

                /* Set this depth's open_path */
                hm->open_path[hover_depth] = new_hover;
                hm->open_depth = hover_depth + 1;

                /* Clear deeper levels if this is a leaf or different branch */
                for (int d = hover_depth + 1; d < HELP_MENU_MAX_DEPTH; d++)
                    hm->open_path[d] = -1;

                /* If it's a category (has children), keep it open for cascade */
                if (hn->child_count > 0) {
                    int d = hover_depth + 2;
                    hm->open_depth = (d <= HELP_MENU_MAX_DEPTH) ? d : HELP_MENU_MAX_DEPTH;
                }

                /* Update tooltip target */
                if (hn->description[0] != '\0') {
                    hm->tooltip_node = new_hover;
                    hm->tooltip_show_node = new_hover;
                } else {
                    hm->tooltip_node = -1;
                }
            }
        }
    } else {
        /* Menu fully closed — clear tooltip target */
        hm->tooltip_node = -1;
    }

    /* Tooltip animation tick */
    float speed = 1.0f / TOOLTIP_ANIM_SECS;
    if (hm->tooltip_node >= 0) {
        hm->tooltip_anim_t += speed * dt;
        if (hm->tooltip_anim_t > 1.0f)
            hm->tooltip_anim_t = 1.0f;
    } else {
        hm->tooltip_anim_t -= speed * dt;
        if (hm->tooltip_anim_t < 0.0f) {
            hm->tooltip_anim_t = 0.0f;
            hm->tooltip_show_node = -1;
        }
    }
}

/* ---- Drawing ---- */

void help_menu_draw(const HelpMenuState *hm, const RenderState *rs)
{
    if (!hm->loaded) return;

    float s = rs->layout.scale;
    Vector2 mouse = GetMousePosition();

    /* Draw "? Help" button */
    Rectangle btn = hm->button_rect;
    bool btn_hover = CheckCollisionPointRec(mouse, btn) || hm->open_depth > 0;

    Color bg = btn_hover ? (Color){25, 40, 25, 220} : (Color){15, 25, 15, 200};
    Color border = btn_hover ? (Color){180, 160, 80, 200} : (Color){60, 80, 60, 150};

    DrawRectangleRounded(btn, 0.2f, 4, bg);
    DrawRectangleRoundedLines(btn, 0.2f, 4, border);

    int btn_fs = (int)(14.0f * s);
    const char *btn_label = "? Help";
    int tw = hh_measure_text(rs, btn_label, btn_fs);
    int tx = (int)(btn.x + (btn.width - (float)tw) * 0.5f);
    int ty = (int)(btn.y + (btn.height - (float)btn_fs) * 0.5f);
    Color text_col = btn_hover ? (Color){220, 200, 120, 255} : LIGHTGRAY;
    hh_draw_text(rs, btn_label, tx, ty, btn_fs, text_col);

    /* Draw cascade columns */
    if (hm->open_depth <= 0) return;

    float item_h = ITEM_HEIGHT_REF * s;
    float pad = COLUMN_PAD_REF * s;
    int item_fs = (int)(14.0f * s);

    /* Column 0: root categories */
    {
        int root_count = 0;
        int root_indices[HELP_MENU_MAX_CHILDREN];
        for (int i = 0; i < hm->node_count; i++) {
            if (hm->nodes[i].parent == -1 && root_count < HELP_MENU_MAX_CHILDREN)
                root_indices[root_count++] = i;
        }

        Rectangle col = hm->column_rects[0];
        DrawRectangleRounded(col, 0.1f, 4, (Color){20, 30, 20, 240});
        DrawRectangleRoundedLines(col, 0.1f, 4, (Color){180, 160, 80, 200});

        for (int i = 0; i < root_count; i++) {
            int idx = root_indices[i];
            const HelpMenuNode *node = &hm->nodes[idx];
            float iy = col.y + item_h * i;
            Rectangle item_rect = {col.x, iy, col.width, item_h};

            /* Highlight hovered item */
            if (hm->hover_node == idx || hm->open_path[0] == idx) {
                DrawRectangleRounded(item_rect, 0.05f, 4, (Color){60, 80, 60, 200});
            }

            /* Draw label */
            int lx = (int)(col.x + pad);
            int ly = (int)(iy + (item_h - (float)item_fs) * 0.5f);
            Color lc = (hm->hover_node == idx) ? (Color){220, 200, 120, 255}
                                                 : LIGHTGRAY;
            hh_draw_text(rs, node->label, lx, ly, item_fs, lc);

            /* Arrow for categories */
            if (node->child_count > 0) {
                int aw = hh_measure_text(rs, ">", item_fs);
                hh_draw_text(rs, ">", (int)(col.x + col.width - pad - (float)aw),
                             ly, item_fs, (Color){140, 130, 70, 200});
            }
        }
    }

    /* Deeper columns */
    for (int d = 0; d < HELP_MENU_MAX_DEPTH - 1; d++) {
        int parent_idx = hm->open_path[d];
        if (parent_idx < 0) break;
        const HelpMenuNode *pn = &hm->nodes[parent_idx];
        if (pn->child_count == 0) break;

        Rectangle col = hm->column_rects[d + 1];
        DrawRectangleRounded(col, 0.1f, 4, (Color){20, 30, 20, 240});
        DrawRectangleRoundedLines(col, 0.1f, 4, (Color){180, 160, 80, 200});

        for (int i = 0; i < pn->child_count; i++) {
            int idx = pn->children[i];
            const HelpMenuNode *node = &hm->nodes[idx];
            float iy = col.y + item_h * i;
            Rectangle item_rect = {col.x, iy, col.width, item_h};

            bool is_hover = (hm->hover_node == idx);
            bool is_open = false;
            if (d + 1 < HELP_MENU_MAX_DEPTH)
                is_open = (hm->open_path[d + 1] == idx);

            if (is_hover || is_open) {
                DrawRectangleRounded(item_rect, 0.05f, 4, (Color){60, 80, 60, 200});
            }

            int lx = (int)(col.x + pad);
            int ly = (int)(iy + (item_h - (float)item_fs) * 0.5f);
            Color lc = is_hover ? (Color){220, 200, 120, 255} : LIGHTGRAY;
            hh_draw_text(rs, node->label, lx, ly, item_fs, lc);

            if (node->child_count > 0) {
                int aw = hh_measure_text(rs, ">", item_fs);
                hh_draw_text(rs, ">", (int)(col.x + col.width - pad - (float)aw),
                             ly, item_fs, (Color){140, 130, 70, 200});
            }
        }
    }
}

/* Word-wrap helper: measure or draw text with wrapping. Returns total height. */
static float help_text_wrapped(const RenderState *rs, const char *text,
                               float x, float y, int font_size,
                               float max_w, Color color, bool draw)
{
    float line_h = (float)font_size + 2.0f;
    float cur_y = y;
    const char *p = text;
    int remaining = (int)strlen(text);

    while (remaining > 0 && *p) {
        int best = 0, best_word = 0;
        for (int i = 1; i <= remaining; i++) {
            char tmp[HELP_MENU_MAX_DESC];
            int n = (i < (int)sizeof(tmp)) ? i : (int)sizeof(tmp) - 1;
            memcpy(tmp, p, n); tmp[n] = '\0';
            if (hh_measure_text(rs, tmp, font_size) > (int)max_w) break;
            best = i;
            /* i == remaining reads null terminator — intentional for best_word */
            if (p[i] == ' ' || p[i] == '\0') best_word = i;
        }
        if (best_word > 0) best = best_word;
        if (best == 0) best = 1;
        int skip = best;
        while (skip < remaining && p[skip] == ' ') skip++;

        if (draw) {
            char tmp[HELP_MENU_MAX_DESC];
            int n = (best < (int)sizeof(tmp)) ? best : (int)sizeof(tmp) - 1;
            memcpy(tmp, p, n); tmp[n] = '\0';
            hh_draw_text(rs, tmp, (int)x, (int)cur_y, font_size, color);
        }
        cur_y += line_h;
        p += skip;
        remaining -= skip;
    }
    return cur_y - y;
}

static float help_draw_wrapped(const RenderState *rs, const char *text,
                               float x, float y, int font_size,
                               float max_w, Color color)
{
    return help_text_wrapped(rs, text, x, y, font_size, max_w, color, true);
}

static float help_measure_wrapped(const RenderState *rs, const char *text,
                                  int font_size, float max_w)
{
    return help_text_wrapped(rs, text, 0, 0, font_size, max_w, WHITE, false);
}

void help_menu_draw_tooltip(const HelpMenuState *hm, const RenderState *rs)
{
    if (!hm->loaded) return;
    if (hm->tooltip_anim_t <= 0.0f) return;

    /* Use tooltip_show_node which persists during shrink-out animation */
    int show_node = hm->tooltip_show_node;
    if (show_node < 0) return;

    const HelpMenuNode *node = &hm->nodes[show_node];
    if (node->description[0] == '\0') return;

    float s = rs->layout.scale;
    float raw_t = hm->tooltip_anim_t;
    float t = ease_apply(EASE_OUT_QUAD, raw_t);

    int name_fs = (int)(14.0f * s);
    int desc_fs = (int)(14.0f * s);
    float tooltip_max_w = TOOLTIP_MAX_W_REF * s;
    float inner_pad = INNER_PAD_REF * s;

    /* Measure text height accurately using word-wrap */
    float text_w = tooltip_max_w - inner_pad * 2;
    float name_h = help_measure_wrapped(rs, node->label, name_fs, text_w);
    float desc_h = help_measure_wrapped(rs, node->description, desc_fs, text_w);
    float gap_between = 3.0f * s;

    /* Account for card sprite height if present */
    float sprite_h = 0.0f;
    if (node->sprite_id >= 0 &&
        card_render_has_transmute_sprite(node->sprite_id)) {
        sprite_h = 120.0f * 0.5f * s + 4.0f * s; /* card height + gap */
    }

    float full_w = tooltip_max_w;
    float full_h = inner_pad + sprite_h + name_h + gap_between + desc_h + inner_pad;

    /* Scale dimensions by animation progress */
    float w = full_w * t;
    float h = full_h * t;

    /* Position: to the right of the deepest open column */
    float col_gap = COLUMN_GAP_REF * s;
    float screen_w = rs->layout.screen_width;
    float screen_h = rs->layout.screen_height;
    float margin = 4.0f * s;

    /* Find the deepest active column */
    int deepest_col = 0;
    for (int d = 0; d < HELP_MENU_MAX_DEPTH - 1; d++) {
        if (hm->open_path[d] >= 0 &&
            hm->nodes[hm->open_path[d]].child_count > 0)
            deepest_col = d + 1;
        else
            break;
    }
    /* The tooltip shows for a leaf in this column */
    Rectangle anchor_col = hm->column_rects[deepest_col];

    /* Find the y position of the hovered item within its column */
    float anchor_y = anchor_col.y;
    if (show_node >= 0) {
        const HelpMenuNode *sn = &hm->nodes[show_node];
        int pidx = sn->parent;
        if (pidx >= 0) {
            const HelpMenuNode *pn = &hm->nodes[pidx];
            float item_h = ITEM_HEIGHT_REF * s;
            for (int i = 0; i < pn->child_count; i++) {
                if (pn->children[i] == show_node) {
                    anchor_y = anchor_col.y + item_h * i;
                    break;
                }
            }
        }
    }

    float tx = anchor_col.x + anchor_col.width + col_gap;
    float ty = anchor_y;

    /* Flip left if it would go off screen */
    if (tx + w > screen_w - margin) {
        tx = anchor_col.x - w - col_gap;
    }
    /* Clamp to screen */
    if (tx < margin) tx = margin;
    if (ty < margin) ty = margin;
    if (ty + h > screen_h - margin) ty = screen_h - margin - h;

    unsigned char bg_alpha = (unsigned char)(245 * t);
    unsigned char border_alpha = (unsigned char)(200 * t);

    /* Background */
    DrawRectangleRounded((Rectangle){tx, ty, w, h}, 0.1f, 4,
                         (Color){20, 30, 20, bg_alpha});
    /* Border */
    DrawRectangleRoundedLines((Rectangle){tx, ty, w, h}, 0.1f, 4,
                              (Color){180, 160, 80, border_alpha});

    /* Only draw text when animation is far enough to be legible */
    if (t > 0.3f) {
        float text_alpha_f = (t - 0.3f) / 0.7f;
        unsigned char text_alpha = (unsigned char)(255 * text_alpha_f);

        float text_x = tx + inner_pad * t;
        float text_y = ty + inner_pad * t;
        float text_max = (full_w - inner_pad * 2) * t;
        if (text_max < 1.0f) text_max = 1.0f;

        int draw_name_fs = (int)(name_fs * t);
        int draw_desc_fs = (int)(desc_fs * t);
        if (draw_name_fs < 1) draw_name_fs = 1;
        if (draw_desc_fs < 1) draw_desc_fs = 1;

        /* Skip text if text_max is too small to fit any character */
        if (text_max < (float)draw_desc_fs) goto done_text;

        /* Draw transmutation card sprite if available */
        float sprite_offset_y = 0.0f;
        if (node->sprite_id >= 0 &&
            card_render_has_transmute_sprite(node->sprite_id)) {
            float card_scale = 0.5f * s * t;
            float card_w = 80.0f * card_scale;
            float card_h = 120.0f * card_scale;
            float card_x = text_x + (text_max - card_w) * 0.5f;
            float card_y = text_y;
            Vector2 pos = {card_x, card_y};
            Vector2 origin = {0, 0};
            card_render_transmute_face(node->sprite_id, pos, card_scale,
                                       text_alpha_f, false, false, 0.0f,
                                       origin);
            sprite_offset_y = card_h + 4.0f * s * t;
        }

        /* Draw title */
        Color name_color = {255, 255, 255, text_alpha};
        Color desc_color = {200, 200, 200, text_alpha};

        float nh = help_draw_wrapped(rs, node->label,
                                     text_x, text_y + sprite_offset_y,
                                     draw_name_fs, text_max, name_color);
        help_draw_wrapped(rs, node->description,
                          text_x, text_y + sprite_offset_y + nh + gap_between * t,
                          draw_desc_fs, text_max, desc_color);
        done_text:;
    }
}
