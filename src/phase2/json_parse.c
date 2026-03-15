#include "json_parse.h"

#include <stdio.h>
#include <string.h>

#include "vendor/cJSON.h"
#include <raylib.h>

/* ============================================================
 * @deps-implements: json_parse.h
 * @deps-requires: json_parse.h, vendor/cJSON.h, raylib.h
 * @deps-last-changed: 2026-03-15 — Initial creation
 * ============================================================ */

/* ----------------------------------------------------------------
 * Enum mapping tables
 * ---------------------------------------------------------------- */

static const EnumMapping CONDITION_TYPE_MAP[] = {
    {"COND_NONE",              COND_NONE},
    {"COND_AVOID_SUIT",        COND_AVOID_SUIT},
    {"COND_COLLECT_N_OF_SUIT", COND_COLLECT_N_OF_SUIT},
    {"COND_WIN_N_TRICKS",      COND_WIN_N_TRICKS},
    {"COND_TAKE_NO_POINTS",    COND_TAKE_NO_POINTS},
    {"COND_TAKE_EXACT_POINTS", COND_TAKE_EXACT_POINTS},
    {"COND_AVOID_CARD",        COND_AVOID_CARD},
    {"COND_COLLECT_CARD",      COND_COLLECT_CARD},
    {"COND_WIN_LAST_TRICK",    COND_WIN_LAST_TRICK},
};

static const EnumMapping EFFECT_TYPE_MAP[] = {
    {"EFFECT_NONE",                 EFFECT_NONE},
    {"EFFECT_POINTS_PER_HEART",     EFFECT_POINTS_PER_HEART},
    {"EFFECT_POINTS_FOR_QOS",       EFFECT_POINTS_FOR_QOS},
    {"EFFECT_FLAT_SCORE_ADJUST",    EFFECT_FLAT_SCORE_ADJUST},
    {"EFFECT_HEARTS_BREAK_EARLY",   EFFECT_HEARTS_BREAK_EARLY},
    {"EFFECT_FORCE_PASS_DIRECTION", EFFECT_FORCE_PASS_DIRECTION},
    {"EFFECT_VOID_SUIT",            EFFECT_VOID_SUIT},
    {"EFFECT_REVEAL_HAND",          EFFECT_REVEAL_HAND},
    {"EFFECT_REVEAL_CONTRACT",      EFFECT_REVEAL_CONTRACT},
    {"EFFECT_SWAP_CARD_POINTS",     EFFECT_SWAP_CARD_POINTS},
};

static const EnumMapping EFFECT_SCOPE_MAP[] = {
    {"EFFECT_SCOPE_SELF",      EFFECT_SCOPE_SELF},
    {"EFFECT_SCOPE_TARGET",    EFFECT_SCOPE_TARGET},
    {"EFFECT_SCOPE_ALL",       EFFECT_SCOPE_ALL},
    {"EFFECT_SCOPE_OPPONENTS", EFFECT_SCOPE_OPPONENTS},
};

static const EnumMapping SUIT_MAP[] = {
    {"SUIT_CLUBS",    SUIT_CLUBS},
    {"SUIT_DIAMONDS", SUIT_DIAMONDS},
    {"SUIT_SPADES",   SUIT_SPADES},
    {"SUIT_HEARTS",   SUIT_HEARTS},
};

static const EnumMapping RANK_MAP[] = {
    {"RANK_2",  RANK_2},  {"RANK_3",  RANK_3},  {"RANK_4",  RANK_4},
    {"RANK_5",  RANK_5},  {"RANK_6",  RANK_6},  {"RANK_7",  RANK_7},
    {"RANK_8",  RANK_8},  {"RANK_9",  RANK_9},  {"RANK_10", RANK_10},
    {"RANK_J",  RANK_J},  {"RANK_Q",  RANK_Q},  {"RANK_K",  RANK_K},
    {"RANK_A",  RANK_A},
};

static const EnumMapping FIGURE_TYPE_MAP[] = {
    {"FIGURE_JACK",  FIGURE_JACK},
    {"FIGURE_QUEEN", FIGURE_QUEEN},
    {"FIGURE_KING",  FIGURE_KING},
};

/* ----------------------------------------------------------------
 * Enum lookup
 * ---------------------------------------------------------------- */

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

int enum_from_string(const EnumMapping *table, int table_size,
                     const char *name, int default_val)
{
    if (!name) return default_val;
    for (int i = 0; i < table_size; i++) {
        if (strcmp(table[i].name, name) == 0) {
            return table[i].value;
        }
    }
    TraceLog(LOG_WARNING, "JSON: Unknown enum value \"%s\"", name);
    return default_val;
}

/* ----------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------- */

/* Safe string copy from a cJSON string item into a fixed-size buffer. */
static void json_strcpy(char *dest, int dest_size, const cJSON *item)
{
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(dest, (size_t)dest_size, "%s", item->valuestring);
    } else {
        dest[0] = '\0';
    }
}

/* Get a string value from a cJSON object field. Returns NULL if missing. */
static const char *json_get_str(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item)) return item->valuestring;
    return NULL;
}

/* Get an int value from a cJSON object field. Returns default_val if missing. */
static int json_get_int(const cJSON *obj, const char *key, int default_val)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) return item->valueint;
    return default_val;
}

/* Parse a Card from a cJSON object {"suit":"SUIT_X","rank":"RANK_Y"}.
 * Returns CARD_NONE if obj is NULL or not an object. */
static Card parse_card(const cJSON *obj)
{
    if (!cJSON_IsObject(obj)) return CARD_NONE;
    Card c;
    c.suit = (Suit)enum_from_string(SUIT_MAP, ARRAY_LEN(SUIT_MAP),
                                    json_get_str(obj, "suit"), -1);
    c.rank = (Rank)enum_from_string(RANK_MAP, ARRAY_LEN(RANK_MAP),
                                    json_get_str(obj, "rank"), -1);
    if ((int)c.suit < 0 || (int)c.rank < 0) return CARD_NONE;
    return c;
}

/* Parse an Effect from a cJSON object {"type":"EFFECT_X","param":{...}}. */
static Effect parse_effect(const cJSON *obj)
{
    Effect e = {0};
    if (!cJSON_IsObject(obj)) return e;

    e.type = (EffectType)enum_from_string(
        EFFECT_TYPE_MAP, ARRAY_LEN(EFFECT_TYPE_MAP),
        json_get_str(obj, "type"), EFFECT_NONE);

    const cJSON *param = cJSON_GetObjectItemCaseSensitive(obj, "param");
    if (cJSON_IsObject(param)) {
        const cJSON *pd = cJSON_GetObjectItemCaseSensitive(param, "points_delta");
        if (cJSON_IsNumber(pd)) {
            e.param.points_delta = pd->valueint;
        }
        const cJSON *vs = cJSON_GetObjectItemCaseSensitive(param, "voided_suit");
        if (cJSON_IsString(vs)) {
            e.param.voided_suit = (Suit)enum_from_string(
                SUIT_MAP, ARRAY_LEN(SUIT_MAP), vs->valuestring, 0);
        }
        const cJSON *fpd = cJSON_GetObjectItemCaseSensitive(param, "pass_direction");
        if (cJSON_IsNumber(fpd)) {
            e.param.pass_direction = fpd->valueint;
        }
    }
    return e;
}

/* Parse a ConditionParam from a cJSON object. */
static ConditionParam parse_condition_param(const cJSON *obj)
{
    ConditionParam cp = {0};
    if (!cJSON_IsObject(obj)) return cp;

    cp.suit = (Suit)enum_from_string(
        SUIT_MAP, ARRAY_LEN(SUIT_MAP),
        json_get_str(obj, "suit"), 0);
    cp.count = json_get_int(obj, "count", 0);
    cp.at_least = cJSON_IsTrue(
        cJSON_GetObjectItemCaseSensitive(obj, "at_least"));

    const cJSON *card_obj = cJSON_GetObjectItemCaseSensitive(obj, "card");
    cp.card = parse_card(card_obj);

    return cp;
}

/* Load file text via Raylib, parse as JSON root. Caller must cJSON_Delete().
 * Returns NULL on failure (logs error). file_text is set for caller to free. */
static cJSON *load_json_file(const char *path, char **file_text)
{
    *file_text = LoadFileText(path);
    if (!*file_text) {
        TraceLog(LOG_WARNING, "JSON: Could not load file \"%s\"", path);
        return NULL;
    }

    cJSON *root = cJSON_Parse(*file_text);
    if (!root) {
        TraceLog(LOG_ERROR, "JSON: Parse error in \"%s\" near: %.20s",
                 path, cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "(unknown)");
        UnloadFileText(*file_text);
        *file_text = NULL;
        return NULL;
    }
    return root;
}

/* ----------------------------------------------------------------
 * Contract loader
 * ---------------------------------------------------------------- */

bool json_load_contracts(const char *path, ContractDef *defs,
                         int max_defs, int *out_count)
{
    *out_count = 0;
    char *file_text = NULL;
    cJSON *root = load_json_file(path, &file_text);
    if (!root) return false;

    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "contracts");
    if (!cJSON_IsArray(arr)) {
        TraceLog(LOG_ERROR, "JSON: \"contracts\" array not found in %s", path);
        cJSON_Delete(root);
        UnloadFileText(file_text);
        return false;
    }

    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr)
    {
        if (count >= max_defs) {
            TraceLog(LOG_WARNING, "JSON: Contract limit %d reached, ignoring extras", max_defs);
            break;
        }

        ContractDef *d = &defs[count];
        memset(d, 0, sizeof(*d));

        d->id = json_get_int(item, "id", count);
        json_strcpy(d->name, sizeof(d->name),
                    cJSON_GetObjectItemCaseSensitive(item, "name"));
        json_strcpy(d->description, sizeof(d->description),
                    cJSON_GetObjectItemCaseSensitive(item, "description"));

        d->condition = (ConditionType)enum_from_string(
            CONDITION_TYPE_MAP, ARRAY_LEN(CONDITION_TYPE_MAP),
            json_get_str(item, "condition"), COND_NONE);

        d->cond_param = parse_condition_param(
            cJSON_GetObjectItemCaseSensitive(item, "condition_param"));

        d->reward_scope = (EffectScope)enum_from_string(
            EFFECT_SCOPE_MAP, ARRAY_LEN(EFFECT_SCOPE_MAP),
            json_get_str(item, "reward_scope"), EFFECT_SCOPE_SELF);

        d->tier = json_get_int(item, "tier", 0);

        /* Parse rewards array */
        const cJSON *rewards = cJSON_GetObjectItemCaseSensitive(item, "rewards");
        if (cJSON_IsArray(rewards)) {
            int ri = 0;
            const cJSON *ritem = NULL;
            cJSON_ArrayForEach(ritem, rewards)
            {
                if (ri >= MAX_CONTRACT_REWARD) break;
                d->rewards[ri] = parse_effect(ritem);
                ri++;
            }
            d->num_rewards = ri;
        }

        count++;
    }

    *out_count = count;
    cJSON_Delete(root);
    UnloadFileText(file_text);
    return true;
}

/* ----------------------------------------------------------------
 * Host action loader
 * ---------------------------------------------------------------- */

bool json_load_host_actions(const char *path, HostActionDef *defs,
                            int max_defs, int *out_count)
{
    *out_count = 0;
    char *file_text = NULL;
    cJSON *root = load_json_file(path, &file_text);
    if (!root) return false;

    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "host_actions");
    if (!cJSON_IsArray(arr)) {
        TraceLog(LOG_ERROR, "JSON: \"host_actions\" array not found in %s", path);
        cJSON_Delete(root);
        UnloadFileText(file_text);
        return false;
    }

    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr)
    {
        if (count >= max_defs) {
            TraceLog(LOG_WARNING, "JSON: Host action limit %d reached", max_defs);
            break;
        }

        HostActionDef *d = &defs[count];
        memset(d, 0, sizeof(*d));

        d->id = json_get_int(item, "id", count);
        json_strcpy(d->name, sizeof(d->name),
                    cJSON_GetObjectItemCaseSensitive(item, "name"));
        json_strcpy(d->description, sizeof(d->description),
                    cJSON_GetObjectItemCaseSensitive(item, "description"));

        d->scope = (EffectScope)enum_from_string(
            EFFECT_SCOPE_MAP, ARRAY_LEN(EFFECT_SCOPE_MAP),
            json_get_str(item, "scope"), EFFECT_SCOPE_ALL);

        const cJSON *effects = cJSON_GetObjectItemCaseSensitive(item, "effects");
        if (cJSON_IsArray(effects)) {
            int ei = 0;
            const cJSON *eitem = NULL;
            cJSON_ArrayForEach(eitem, effects)
            {
                if (ei >= MAX_HOST_EFFECTS) break;
                d->effects[ei] = parse_effect(eitem);
                ei++;
            }
            d->num_effects = ei;
        }

        count++;
    }

    *out_count = count;
    cJSON_Delete(root);
    UnloadFileText(file_text);
    return true;
}

/* ----------------------------------------------------------------
 * Revenge loader
 * ---------------------------------------------------------------- */

bool json_load_revenges(const char *path, RevengeDef *defs,
                        int max_defs, int *out_count)
{
    *out_count = 0;
    char *file_text = NULL;
    cJSON *root = load_json_file(path, &file_text);
    if (!root) return false;

    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "revenges");
    if (!cJSON_IsArray(arr)) {
        TraceLog(LOG_ERROR, "JSON: \"revenges\" array not found in %s", path);
        cJSON_Delete(root);
        UnloadFileText(file_text);
        return false;
    }

    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr)
    {
        if (count >= max_defs) {
            TraceLog(LOG_WARNING, "JSON: Revenge limit %d reached", max_defs);
            break;
        }

        RevengeDef *d = &defs[count];
        memset(d, 0, sizeof(*d));

        d->id = json_get_int(item, "id", count);
        json_strcpy(d->name, sizeof(d->name),
                    cJSON_GetObjectItemCaseSensitive(item, "name"));
        json_strcpy(d->description, sizeof(d->description),
                    cJSON_GetObjectItemCaseSensitive(item, "description"));

        d->scope = (EffectScope)enum_from_string(
            EFFECT_SCOPE_MAP, ARRAY_LEN(EFFECT_SCOPE_MAP),
            json_get_str(item, "scope"), EFFECT_SCOPE_TARGET);

        const cJSON *effects = cJSON_GetObjectItemCaseSensitive(item, "effects");
        if (cJSON_IsArray(effects)) {
            int ei = 0;
            const cJSON *eitem = NULL;
            cJSON_ArrayForEach(eitem, effects)
            {
                if (ei >= MAX_REVENGE_EFFECTS) break;
                d->effects[ei] = parse_effect(eitem);
                ei++;
            }
            d->num_effects = ei;
        }

        count++;
    }

    *out_count = count;
    cJSON_Delete(root);
    UnloadFileText(file_text);
    return true;
}

/* ----------------------------------------------------------------
 * Character loader
 * ---------------------------------------------------------------- */

bool json_load_characters(const char *path, CharacterDef *defs,
                          int max_defs, int *out_count)
{
    *out_count = 0;
    char *file_text = NULL;
    cJSON *root = load_json_file(path, &file_text);
    if (!root) return false;

    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "characters");
    if (!cJSON_IsArray(arr)) {
        TraceLog(LOG_ERROR, "JSON: \"characters\" array not found in %s", path);
        cJSON_Delete(root);
        UnloadFileText(file_text);
        return false;
    }

    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr)
    {
        if (count >= max_defs) {
            TraceLog(LOG_WARNING, "JSON: Character limit %d reached", max_defs);
            break;
        }

        CharacterDef *d = &defs[count];
        memset(d, 0, sizeof(*d));

        d->id = json_get_int(item, "id", count);
        json_strcpy(d->name, sizeof(d->name),
                    cJSON_GetObjectItemCaseSensitive(item, "name"));
        json_strcpy(d->description, sizeof(d->description),
                    cJSON_GetObjectItemCaseSensitive(item, "description"));
        json_strcpy(d->portrait_asset, sizeof(d->portrait_asset),
                    cJSON_GetObjectItemCaseSensitive(item, "portrait_asset"));
        json_strcpy(d->card_art_asset, sizeof(d->card_art_asset),
                    cJSON_GetObjectItemCaseSensitive(item, "card_art_asset"));

        d->figure_type = (FigureType)enum_from_string(
            FIGURE_TYPE_MAP, ARRAY_LEN(FIGURE_TYPE_MAP),
            json_get_str(item, "figure_type"), FIGURE_JACK);

        /* Parse mechanics based on figure_type */
        const cJSON *mech = cJSON_GetObjectItemCaseSensitive(item, "mechanics");
        if (cJSON_IsObject(mech)) {
            switch (d->figure_type) {
            case FIGURE_KING: {
                /* Default all tiers to -1 (no contract) */
                for (int i = 0; i < CONTRACT_TIERS; i++)
                    d->mechanics.king.contract_ids[i] = -1;
                const cJSON *king = cJSON_GetObjectItemCaseSensitive(mech, "king");
                if (cJSON_IsObject(king)) {
                    const cJSON *ids = cJSON_GetObjectItemCaseSensitive(king, "contract_ids");
                    if (cJSON_IsArray(ids)) {
                        for (int i = 0; i < CONTRACT_TIERS && i < cJSON_GetArraySize(ids); i++) {
                            const cJSON *v = cJSON_GetArrayItem(ids, i);
                            d->mechanics.king.contract_ids[i] =
                                cJSON_IsNumber(v) ? v->valueint : -1;
                        }
                    }
                }
                break;
            }
            case FIGURE_QUEEN: {
                const cJSON *queen = cJSON_GetObjectItemCaseSensitive(mech, "queen");
                if (cJSON_IsObject(queen)) {
                    const cJSON *ids = cJSON_GetObjectItemCaseSensitive(queen, "revenge_ids");
                    if (cJSON_IsArray(ids)) {
                        int n = 0;
                        int arr_size = cJSON_GetArraySize(ids);
                        for (int i = 0; i < arr_size && i < MAX_CHAR_REVENGES; i++) {
                            const cJSON *v = cJSON_GetArrayItem(ids, i);
                            int val = cJSON_IsNumber(v) ? v->valueint : -1;
                            if (val >= 0) {
                                d->mechanics.queen.revenge_ids[n] = val;
                                n++;
                            }
                        }
                        d->mechanics.queen.num_revenges = n;
                    }
                }
                break;
            }
            case FIGURE_JACK: {
                const cJSON *jack = cJSON_GetObjectItemCaseSensitive(mech, "jack");
                if (cJSON_IsObject(jack)) {
                    const cJSON *ids = cJSON_GetObjectItemCaseSensitive(jack, "host_action_ids");
                    if (cJSON_IsArray(ids)) {
                        int n = 0;
                        int arr_size = cJSON_GetArraySize(ids);
                        for (int i = 0; i < arr_size && i < MAX_CHAR_HOST_ACTIONS; i++) {
                            const cJSON *v = cJSON_GetArrayItem(ids, i);
                            int val = cJSON_IsNumber(v) ? v->valueint : -1;
                            if (val >= 0) {
                                d->mechanics.jack.host_action_ids[n] = val;
                                n++;
                            }
                        }
                        d->mechanics.jack.num_host_actions = n;
                    }
                }
                break;
            }
            default:
                break;
            }
        }

        count++;
    }

    *out_count = count;
    cJSON_Delete(root);
    UnloadFileText(file_text);
    return true;
}
