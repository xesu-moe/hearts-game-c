/* ============================================================
 * resource.h — Asset loading abstraction for dev/distribution builds.
 *
 * In dev mode (default): passthrough to Raylib file-loading functions.
 * In dist mode (-DHH_EMBEDDED): loads from byte arrays compiled into the binary.
 * ============================================================ */
#ifndef RESOURCE_H
#define RESOURCE_H

#include "raylib.h"

/* Load a texture from a PNG file path */
Texture2D res_load_texture(const char *path);

/* Load a font at a specific size from a TTF file path */
Font res_load_font(const char *path, int font_size);

/* Load a music stream from an OGG file path.
 * In embedded mode, the data is static and lives for the program lifetime. */
Music res_load_music(const char *path);

/* Load a sound effect from an OGG file path */
Sound res_load_sound(const char *path);

/* Load a shader. vs_path may be NULL for fragment-only shaders. */
Shader res_load_shader(const char *vs_path, const char *fs_path);

/* Read a file into a malloc'd buffer (null-terminated). Returns NULL on failure.
 * Caller must free(). Falls through to fopen for paths not in the embedded table
 * (e.g. settings.json). */
char *res_read_file_text(const char *path);

#endif /* RESOURCE_H */
