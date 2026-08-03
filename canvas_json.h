#ifndef DRAW_APP_CANVAS_JSON_H
#define DRAW_APP_CANVAS_JSON_H

#include "canvas.h"

/*
 * Produces a NUL-terminated JSON string owned by the caller. The operation
 * list is rooted at history.head and recursively nested through each `next`
 * field. Pointer-valued back references are represented by a zero-based node
 * index or null. A document with revision above
 * CANVAS_DOCUMENT_MAX_REVISION is invalid.
 */
TgResult canvas_document_dump_json(
    const CanvasDocument *document,
    char **out_json);

/*
 * Initializes out_document from JSON. out_document must not already own a
 * document; destroy the returned value with canvas_document_destroy().
 */
TgResult canvas_document_load_json(
    const char *json,
    CanvasDocument *out_document);

TgResult canvas_document_save_json_file(
    const CanvasDocument *document,
    const char *path);

TgResult canvas_document_load_json_file(
    const char *path,
    CanvasDocument *out_document);

#endif
