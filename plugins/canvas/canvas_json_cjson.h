#ifndef DRAW_APP_PLUGIN_CANVAS_JSON_CJSON_H
#define DRAW_APP_PLUGIN_CANVAS_JSON_CJSON_H

#include "canvas.h"

/*
 * cJSON-backed implementation of the schema declared in canvas_json.h.
 * Parsed integer values above 2^53 - 1 return TG_ERR_UNSUPPORTED because
 * cJSON represents JSON numbers as double.
 */
TgResult canvas_document_dump_json_cjson(
    const CanvasDocument *document,
    char **out_json);

TgResult canvas_document_load_json_cjson(
    const char *json,
    CanvasDocument *out_document);

TgResult canvas_document_save_json_file_cjson(
    const CanvasDocument *document,
    const char *path);

TgResult canvas_document_load_json_file_cjson(
    const char *path,
    CanvasDocument *out_document);

#endif
