/* ============================================================
 * @deps-implements: todo_panel.h
 * @deps-requires: cJSON.h (cJSON_*), resource.h (res_read_file_text), raylib.h
 * ============================================================ */
#include "todo_panel.h"
#include "render.h"
#include "vendor/cJSON.h"
#include "core/resource.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Colors — same green-tinted theme as friend panel */
#define COL_PANEL_BG       CLITERAL(Color){20, 30, 20, 240}
#define COL_PANEL_BORDER   CLITERAL(Color){180, 160, 80, 200}
#define COL_TEXT            CLITERAL(Color){220, 220, 220, 255}
#define COL_TEXT_DIM        CLITERAL(Color){140, 140, 160, 255}
#define COL_SECTION_TITLE  CLITERAL(Color){220, 180, 50, 255}

#define FONT_SIZE      26
#define FONT_SM        18
#define FONT_SPACING   1
#define TITLE_HEIGHT   32
#define SECTION_HEADER 24
#define ITEM_HEIGHT    20
#define SECTION_GAP     8
#define PADDING        10

/* ================================================================
 * JSON loading
 * ================================================================ */

static int load_array(const cJSON *root, const char *key,
                      char out[][TODO_MAX_ITEM_LEN], int max)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsArray(arr)) return 0;

    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        if (count >= max) break;
        if (cJSON_IsString(item) && item->valuestring) {
            strncpy(out[count], item->valuestring, TODO_MAX_ITEM_LEN - 1);
            out[count][TODO_MAX_ITEM_LEN - 1] = '\0';
            count++;
        }
    }
    return count;
}

void todo_panel_init(TodoPanelState *state)
{
    memset(state, 0, sizeof(*state));

    char *text = res_read_file_text("assets/defs/todo.json");
    if (!text) {
        fprintf(stderr, "todo_panel: could not load assets/defs/todo.json\n");
        return;
    }

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        fprintf(stderr, "todo_panel: JSON parse error\n");
        return;
    }

    state->known_bugs_count = load_array(root, "known_bugs",
                                         state->known_bugs, TODO_MAX_ITEMS);
    state->testing_required_count = load_array(root, "testing_required",
                                               state->testing_required, TODO_MAX_ITEMS);
    state->planned_features_count = load_array(root, "planned_features",
                                               state->planned_features, TODO_MAX_ITEMS);

    cJSON_Delete(root);
    state->loaded = true;
}

/* ================================================================
 * Height calculation
 * ================================================================ */

static float section_height(int count, float s)
{
    if (count <= 0) return 0;
    return SECTION_HEADER * s + count * ITEM_HEIGHT * s + SECTION_GAP * s;
}

float todo_panel_height(const TodoPanelState *state, float scale)
{
    float s = scale;
    return TITLE_HEIGHT * s
         + section_height(state->known_bugs_count, s)
         + section_height(state->testing_required_count, s)
         + section_height(state->planned_features_count, s)
         + PADDING * s;
}

/* ================================================================
 * Drawing
 * ================================================================ */

static float draw_section(const RenderState *rs, float x, float y, float w,
                          const char *title, int count,
                          const char items[][TODO_MAX_ITEM_LEN], float s)
{
    if (count <= 0) return y;

    int font_sm = (int)(FONT_SM * s);
    int font_hdr = (int)((FONT_SM + 2) * s);

    /* Section header */
    char header[64];
    snprintf(header, sizeof(header), "%s (%d)", title, count);
    hh_draw_text(rs, header, (int)(x + 8 * s), (int)y, font_hdr, COL_SECTION_TITLE);
    y += SECTION_HEADER * s;

    /* Items */
    int max_text_w = (int)(w - 24 * s);
    for (int i = 0; i < count; i++) {
        /* Build display string with bullet */
        char display[TODO_MAX_ITEM_LEN + 4];
        snprintf(display, sizeof(display), "- %s", items[i]);

        /* Truncate if too wide */
        int tw = hh_measure_text(rs, display, font_sm);
        if (tw > max_text_w) {
            int len = (int)strlen(display);
            while (len > 4 && tw > max_text_w) {
                len -= 2;
                display[len] = '\0';
                strcat(display, "...");
                tw = hh_measure_text(rs, display, font_sm);
            }
        }

        hh_draw_text(rs, display, (int)(x + 14 * s), (int)y, font_sm, COL_TEXT_DIM);
        y += ITEM_HEIGHT * s;
    }

    y += SECTION_GAP * s;
    return y;
}

void todo_panel_draw(const TodoPanelState *state, Rectangle pr,
                     const RenderState *rs)
{
    float s = pr.width / (float)TODO_PANEL_WIDTH;

    /* Panel background */
    DrawRectangleRounded(pr, 0.05f, 4, COL_PANEL_BG);
    DrawRectangleRoundedLines(pr, 0.05f, 4, COL_PANEL_BORDER);

    /* Title */
    {
        const char *title = "Dev Status";
        int font_title = (int)(FONT_SIZE * s);
        int tw = hh_measure_text(rs, title, font_title);
        hh_draw_text(rs, title, (int)(pr.x + (pr.width - (float)tw) / 2.0f),
                     (int)(pr.y + 4 * s), font_title, COL_TEXT);
    }

    float y = pr.y + TITLE_HEIGHT * s;

    y = draw_section(rs, pr.x, y, pr.width,
                     "Known Bugs", state->known_bugs_count, state->known_bugs, s);
    y = draw_section(rs, pr.x, y, pr.width,
                     "Testing Required", state->testing_required_count, state->testing_required, s);
    y = draw_section(rs, pr.x, y, pr.width,
                     "Planned Features", state->planned_features_count, state->planned_features, s);
    (void)y;
}
