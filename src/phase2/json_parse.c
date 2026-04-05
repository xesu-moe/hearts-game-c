/* ============================================================
 * @deps-implements: json_parse.h
 * @deps-requires: json_parse.h, contract.h (ConditionType), transmutation.h (TransmuteEffect, TEFFECT_BOUNTY_REDIRECT_QOS, TEFFECT_INVERSION_NEGATE_POINTS, TEFFECT_JOKER_LEAD_WIN),
 *                 vendor/cJSON.h, stdio.h, stdlib.h, string.h
 * @deps-last-changed: 2026-03-22 — Removed raylib.h LoadFileText, replaced with fopen/fread/fclose
 * ============================================================ */

#include "json_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HH_EMBEDDED
#include "core/resource.h"
#endif
#include "vendor/cJSON.h"

/* ----------------------------------------------------------------
 * Enum mapping tables
 * ---------------------------------------------------------------- */

static const EnumMapping CONDITION_TYPE_MAP[] = {
    {"COND_NONE",                    COND_NONE},
    {"COND_AVOID_SUIT",              COND_AVOID_SUIT},
    {"COND_COLLECT_N_OF_SUIT",       COND_COLLECT_N_OF_SUIT},
    {"COND_WIN_N_TRICKS",            COND_WIN_N_TRICKS},
    {"COND_TAKE_NO_POINTS",          COND_TAKE_NO_POINTS},
    {"COND_TAKE_EXACT_POINTS",       COND_TAKE_EXACT_POINTS},
    {"COND_AVOID_CARD",              COND_AVOID_CARD},
    {"COND_COLLECT_CARD",            COND_COLLECT_CARD},
    {"COND_WIN_CONSECUTIVE_TRICKS",  COND_WIN_CONSECUTIVE_TRICKS},
    {"COND_HIT_N_WITH_SUIT",         COND_HIT_N_WITH_SUIT},
    {"COND_LOWEST_SCORE",            COND_LOWEST_SCORE},
    {"COND_NEVER_LEAD_SUIT",         COND_NEVER_LEAD_SUIT},
    {"COND_WIN_TRICK_N",             COND_WIN_TRICK_N},
    {"COND_BREAK_HEARTS",            COND_BREAK_HEARTS},
    {"COND_WIN_FIRST_N_TRICKS",      COND_WIN_FIRST_N_TRICKS},
    {"COND_AVOID_LAST_N_TRICKS",     COND_AVOID_LAST_N_TRICKS},
    {"COND_WIN_WITH_PASSED_CARD",    COND_WIN_WITH_PASSED_CARD},
    {"COND_HIT_WITH_PASSED_CARD",    COND_HIT_WITH_PASSED_CARD},
    {"COND_WIN_FIRST_AND_LAST",      COND_WIN_FIRST_AND_LAST},
    {"COND_LEAD_QUEEN_SPADES_TRICK", COND_LEAD_QUEEN_SPADES_TRICK},
    {"COND_SHOOT_THE_MOON",          COND_SHOOT_THE_MOON},
    {"COND_PREVENT_MOON",            COND_PREVENT_MOON},
    {"COND_PLAY_CARD_FIRST_OF_SUIT", COND_PLAY_CARD_FIRST_OF_SUIT},
    {"COND_HIT_WITH_TRANSMUTE",      COND_HIT_WITH_TRANSMUTE},
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

static const EnumMapping TRANSMUTE_SPECIAL_MAP[] = {
    {"TRANSMUTE_NORMAL",      TRANSMUTE_NORMAL},
    {"TRANSMUTE_ALWAYS_WIN",  TRANSMUTE_ALWAYS_WIN},
    {"TRANSMUTE_ALWAYS_LOSE", TRANSMUTE_ALWAYS_LOSE},
};

static const EnumMapping TRANSMUTE_EFFECT_MAP[] = {
    {"EFFECT_NONE",                    TEFFECT_NONE},
    {"WOTT_DUPLICATE_ROUND_POINTS",    TEFFECT_WOTT_DUPLICATE_ROUND_POINTS},
    {"WOTT_REDUCE_SCORE_3",            TEFFECT_WOTT_REDUCE_SCORE_3},
    {"WOTT_REVEAL_OPPONENT_CARD",      TEFFECT_WOTT_REVEAL_OPPONENT_CARD},
    {"WOTT_REDUCE_SCORE_1",            TEFFECT_WOTT_REDUCE_SCORE_1},
    {"WOTT_SWAP_CARD",                 TEFFECT_WOTT_SWAP_CARD},
    {"FOG_HIDDEN",                     TEFFECT_FOG_HIDDEN},
    {"MIRROR_COPY_EFFECTS",            TEFFECT_MIRROR},
    {"RANDOM_TRICK_WINNER",            TEFFECT_RANDOM_TRICK_WINNER},
    {"TRAP_DOUBLE_WITH_QOS",           TEFFECT_TRAP_DOUBLE_WITH_QOS},
    {"WOTT_SHIELD_NEXT_TRICK",         TEFFECT_WOTT_SHIELD_NEXT_TRICK},
    {"WOTT_FORCE_LEAD_HEARTS",         TEFFECT_WOTT_FORCE_LEAD_HEARTS},
    {"ANCHOR_FORCE_LEAD_SUIT",         TEFFECT_ANCHOR_FORCE_LEAD_SUIT},
    {"BINDING_AUTO_WIN_NEXT",          TEFFECT_BINDING_AUTO_WIN_NEXT},
    {"CROWN_HIGHEST_RANK",             TEFFECT_CROWN_HIGHEST_RANK},
    {"PARASITE_REDIRECT_POINTS",       TEFFECT_PARASITE_REDIRECT_POINTS},
    {"BOUNTY_REDIRECT_QOS",            TEFFECT_BOUNTY_REDIRECT_QOS},
    {"INVERSION_NEGATE_POINTS",        TEFFECT_INVERSION_NEGATE_POINTS},
    {"JOKER_LEAD_WIN",                 TEFFECT_JOKER_LEAD_WIN},
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
    fprintf(stderr, "JSON: Unknown enum value \"%s\"\n", name);
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

    cp.trick_num = json_get_int(obj, "trick_num", 0);

    const cJSON *card_obj = cJSON_GetObjectItemCaseSensitive(obj, "card");
    cp.card = parse_card(card_obj);

    return cp;
}

/* Read entire file into malloc'd buffer. Returns NULL on failure.
 * In embedded builds (HH_EMBEDDED), checks compiled-in data first. */
static char *read_file_text(const char *path)
{
#ifdef HH_EMBEDDED
    return res_read_file_text(path);
#else
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
#endif
}

/* Load file text, parse as JSON root. Caller must cJSON_Delete().
 * Returns NULL on failure (logs error). file_text is set for caller to free. */
static cJSON *load_json_file(const char *path, char **file_text)
{
    *file_text = read_file_text(path);
    if (!*file_text) {
        fprintf(stderr, "JSON: Could not load file \"%s\"\n", path);
        return NULL;
    }

    cJSON *root = cJSON_Parse(*file_text);
    if (!root) {
        fprintf(stderr, "JSON: Parse error in \"%s\" near: %.20s\n",
                 path, cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "(unknown)");
        free(*file_text);
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
        fprintf(stderr, "JSON: \"contracts\" array not found in %s\n", path);
        cJSON_Delete(root);
        free(file_text);
        return false;
    }

    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr)
    {
        if (count >= max_defs) {
            fprintf(stderr, "JSON: Contract limit %d reached, ignoring extras\n", max_defs);
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

        /* Parse transmutation reward IDs (optional) */
        for (int tr = 0; tr < MAX_CONTRACT_TRANSMUTE_REWARD; tr++)
            d->transmute_reward_ids[tr] = -1;
        d->num_transmute_rewards = 0;
        const cJSON *trewards = cJSON_GetObjectItemCaseSensitive(item, "transmute_rewards");
        if (cJSON_IsArray(trewards)) {
            int ti = 0;
            int tarr_size = cJSON_GetArraySize(trewards);
            for (int tr = 0; tr < tarr_size && ti < MAX_CONTRACT_TRANSMUTE_REWARD; tr++) {
                const cJSON *tv = cJSON_GetArrayItem(trewards, tr);
                if (cJSON_IsNumber(tv)) {
                    d->transmute_reward_ids[ti] = tv->valueint;
                    ti++;
                }
            }
            d->num_transmute_rewards = ti;
        }

        count++;
    }

    *out_count = count;
    cJSON_Delete(root);
    free(file_text);
    return true;
}

/* ----------------------------------------------------------------
 * Transmutation loader
 * ---------------------------------------------------------------- */

bool json_load_transmutations(const char *path, TransmutationDef *defs,
                              int max_defs, int *out_count)
{
    *out_count = 0;
    char *file_text = NULL;
    cJSON *root = load_json_file(path, &file_text);
    if (!root) return false;

    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "transmutations");
    if (!cJSON_IsArray(arr)) {
        fprintf(stderr, "JSON: \"transmutations\" array not found in %s\n", path);
        cJSON_Delete(root);
        free(file_text);
        return false;
    }

    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr)
    {
        if (count >= max_defs) {
            fprintf(stderr, "JSON: Transmutation limit %d reached\n", max_defs);
            break;
        }

        TransmutationDef *d = &defs[count];
        memset(d, 0, sizeof(*d));

        d->id = json_get_int(item, "id", count);
        json_strcpy(d->name, sizeof(d->name),
                    cJSON_GetObjectItemCaseSensitive(item, "name"));
        json_strcpy(d->description, sizeof(d->description),
                    cJSON_GetObjectItemCaseSensitive(item, "description"));

        d->result_suit = (Suit)enum_from_string(
            SUIT_MAP, ARRAY_LEN(SUIT_MAP),
            json_get_str(item, "result_suit"), SUIT_CLUBS);

        d->result_rank = (Rank)enum_from_string(
            RANK_MAP, ARRAY_LEN(RANK_MAP),
            json_get_str(item, "result_rank"), RANK_2);

        d->special = (TransmuteSpecial)enum_from_string(
            TRANSMUTE_SPECIAL_MAP, ARRAY_LEN(TRANSMUTE_SPECIAL_MAP),
            json_get_str(item, "special"), TRANSMUTE_NORMAL);

        /* Parse suit_mask as array of suit strings, OR'd into bitmask */
        d->suit_mask = SUIT_MASK_NONE;
        const cJSON *mask_arr = cJSON_GetObjectItemCaseSensitive(item, "suit_mask");
        if (cJSON_IsArray(mask_arr)) {
            const cJSON *ms = NULL;
            cJSON_ArrayForEach(ms, mask_arr)
            {
                if (cJSON_IsString(ms)) {
                    int sv = enum_from_string(SUIT_MAP, ARRAY_LEN(SUIT_MAP),
                                             ms->valuestring, -1);
                    if (sv >= 0) {
                        d->suit_mask |= (1 << sv);
                    }
                }
            }
        }

        d->custom_points = json_get_int(item, "custom_points", -1);
        d->negative = cJSON_IsTrue(
            cJSON_GetObjectItemCaseSensitive(item, "negative"));
        d->hide_in_scoring = cJSON_IsTrue(
            cJSON_GetObjectItemCaseSensitive(item, "hide_in_scoring"));
        json_strcpy(d->art_asset, sizeof(d->art_asset),
                    cJSON_GetObjectItemCaseSensitive(item, "art_asset"));

        d->effect = (TransmuteEffect)enum_from_string(
            TRANSMUTE_EFFECT_MAP, ARRAY_LEN(TRANSMUTE_EFFECT_MAP),
            json_get_str(item, "effect"), TEFFECT_NONE);

        count++;
    }

    *out_count = count;
    cJSON_Delete(root);
    free(file_text);
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
        fprintf(stderr, "JSON: \"characters\" array not found in %s\n", path);
        cJSON_Delete(root);
        free(file_text);
        return false;
    }

    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr)
    {
        if (count >= max_defs) {
            fprintf(stderr, "JSON: Character limit %d reached\n", max_defs);
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
            case FIGURE_QUEEN:
                /* Queen mechanics reserved for future use */
                d->mechanics.queen._reserved = 0;
                break;
            case FIGURE_JACK:
                /* Jack mechanics reserved for future use */
                d->mechanics.jack._reserved = 0;
                break;
            default:
                break;
            }
        }

        count++;
    }

    *out_count = count;
    cJSON_Delete(root);
    free(file_text);
    return true;
}
