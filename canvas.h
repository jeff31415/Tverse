#ifndef DRAW_APP_CANVAS_H
#define DRAW_APP_CANVAS_H

#include "tui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum CanvasOperationType {
    CANVAS_OPERATION_DRAW_CELLS = 0
} CanvasOperationType;

typedef struct CanvasSample {
    TgVec2i position;
    TuiCell cell;
} CanvasSample;

typedef struct CanvasOperation CanvasOperation;

typedef struct CanvasHistory {
    CanvasOperation *head;
    CanvasOperation *tail;
    CanvasOperation *cursor;
    size_t operation_count;
} CanvasHistory;

typedef struct CanvasDocument {
    TgSizei output_size;
    CanvasHistory history;
    uint64_t revision;
} CanvasDocument;

typedef void (*CanvasReplayFn)(const CanvasSample *sample, void *userdata);

TgResult canvas_document_init(CanvasDocument *document, TgSizei output_size);
void canvas_document_destroy(CanvasDocument *document);
void canvas_document_reset(CanvasDocument *document);

TgResult canvas_document_commit_draw(
    CanvasDocument *document,
    const CanvasSample *samples,
    size_t sample_count);

bool canvas_document_can_undo(const CanvasDocument *document);
bool canvas_document_can_redo(const CanvasDocument *document);
bool canvas_document_undo(CanvasDocument *document);
bool canvas_document_redo(CanvasDocument *document);

void canvas_document_replay(
    const CanvasDocument *document,
    CanvasReplayFn replay,
    void *userdata);

bool canvas_document_contains_output(
    const CanvasDocument *document,
    TgVec2i position);

#endif
