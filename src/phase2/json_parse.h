#ifndef JSON_PARSE_H
#define JSON_PARSE_H

/* ============================================================
 * @deps-exports: EnumMapping, enum_from_string(),
 *                json_load_contracts(),
 *                json_load_transmutations(), json_load_characters()
 * @deps-requires: contract.h, transmutation.h, character.h
 * @deps-used-by: phase2_defs.c
 * @deps-last-changed: 2026-03-22 — Removed json_load_vendettas
 * ============================================================ */

#include <stdbool.h>

#include "character.h"
#include "contract.h"
#include "transmutation.h"

/* --- Enum Mapping --- */

typedef struct EnumMapping {
    const char *name;
    int         value;
} EnumMapping;

/* Look up a C enum value from its JSON string name.
 * Returns default_val if name is NULL or not found (logs a warning). */
int enum_from_string(const EnumMapping *table, int table_size,
                     const char *name, int default_val);

/* --- JSON Loaders ---
 * Each loader reads a JSON file, parses the array, and populates
 * the output array up to max_defs. Sets *out_count to the number loaded.
 * Returns true on success (even partial), false if file missing or
 * JSON parse failed. */

bool json_load_contracts(const char *path, ContractDef *defs,
                         int max_defs, int *out_count);

bool json_load_transmutations(const char *path, TransmutationDef *defs,
                              int max_defs, int *out_count);

bool json_load_characters(const char *path, CharacterDef *defs,
                          int max_defs, int *out_count);

#endif /* JSON_PARSE_H */
