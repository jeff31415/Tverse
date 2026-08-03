#ifndef DRAW_APP_CANVAS_JSON_JANSSON_H
#define DRAW_APP_CANVAS_JSON_JANSSON_H

#include "canvas.h"

/* Jansson-backed implementation of the schema declared in canvas_json.h. */
TgResult canvas_document_dump_json_jansson(
    const CanvasDocument *document,
    char **out_json);

TgResult canvas_document_load_json_jansson(
    const char *json,
    CanvasDocument *out_document);

TgResult canvas_document_save_json_file_jansson(
    const CanvasDocument *document,
    const char *path);

TgResult canvas_document_load_json_file_jansson(
    const char *path,
    CanvasDocument *out_document);

#endif
