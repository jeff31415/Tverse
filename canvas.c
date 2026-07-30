#include "canvas.h"

#include <stdlib.h>
#include <string.h>

struct CanvasOperation {
    CanvasOperationType type;
    CanvasOperation *prev;
    CanvasOperation *next;
    CanvasSample *samples;
    size_t sample_count;
};

static void canvas_document_touch(CanvasDocument *document)
{
    if (document != NULL && document->revision < UINT64_MAX) {
        ++document->revision;
    }
}

static size_t canvas_operation_free_chain(CanvasOperation *operation)
{
    size_t count = 0;
    while (operation != NULL) {
        CanvasOperation *next = operation->next;
        free(operation->samples);
        free(operation);
        operation = next;
        if (count < SIZE_MAX) {
            ++count;
        }
    }
    return count;
}

TgResult canvas_document_init(CanvasDocument *document, TgSizei output_size)
{
    if (document == NULL || output_size.w <= 0 || output_size.h <= 0) {
        return TG_ERR_INVALID;
    }

    memset(document, 0, sizeof(*document));
    document->output_size = output_size;
    return TG_OK;
}

void canvas_document_destroy(CanvasDocument *document)
{
    if (document == NULL) {
        return;
    }

    (void)canvas_operation_free_chain(document->history.head);
    memset(document, 0, sizeof(*document));
}

void canvas_document_reset(CanvasDocument *document)
{
    if (document == NULL) {
        return;
    }

    (void)canvas_operation_free_chain(document->history.head);
    memset(&document->history, 0, sizeof(document->history));
    canvas_document_touch(document);
}

TgResult canvas_document_commit_draw(
    CanvasDocument *document,
    const CanvasSample *samples,
    size_t sample_count)
{
    if (document == NULL || (sample_count > 0 && samples == NULL)) {
        return TG_ERR_INVALID;
    }
    if (sample_count == 0) {
        return TG_OK;
    }
    if (sample_count > SIZE_MAX / sizeof(*samples)) {
        return TG_ERR_NOMEM;
    }

    CanvasOperation *operation = calloc(1, sizeof(*operation));
    if (operation == NULL) {
        return TG_ERR_NOMEM;
    }

    operation->samples = malloc(sample_count * sizeof(*operation->samples));
    if (operation->samples == NULL) {
        free(operation);
        return TG_ERR_NOMEM;
    }

    memcpy(
        operation->samples,
        samples,
        sample_count * sizeof(*operation->samples));
    operation->type = CANVAS_OPERATION_DRAW_CELLS;
    operation->sample_count = sample_count;

    CanvasHistory *history = &document->history;
    CanvasOperation *redo = history->cursor != NULL
        ? history->cursor->next
        : history->head;

    if (redo != NULL) {
        if (history->cursor != NULL) {
            history->cursor->next = NULL;
        } else {
            history->head = NULL;
        }
        size_t removed = canvas_operation_free_chain(redo);
        history->operation_count =
            removed <= history->operation_count
            ? history->operation_count - removed
            : 0;
    }

    operation->prev = history->cursor;
    if (history->cursor != NULL) {
        history->cursor->next = operation;
    } else {
        history->head = operation;
    }
    history->tail = operation;
    history->cursor = operation;

    if (history->operation_count < SIZE_MAX) {
        ++history->operation_count;
    }
    canvas_document_touch(document);
    return TG_OK;
}

bool canvas_document_can_undo(const CanvasDocument *document)
{
    return document != NULL && document->history.cursor != NULL;
}

bool canvas_document_can_redo(const CanvasDocument *document)
{
    if (document == NULL) {
        return false;
    }
    if (document->history.cursor == NULL) {
        return document->history.head != NULL;
    }
    return document->history.cursor->next != NULL;
}

bool canvas_document_undo(CanvasDocument *document)
{
    if (!canvas_document_can_undo(document)) {
        return false;
    }

    document->history.cursor = document->history.cursor->prev;
    canvas_document_touch(document);
    return true;
}

bool canvas_document_redo(CanvasDocument *document)
{
    if (!canvas_document_can_redo(document)) {
        return false;
    }

    document->history.cursor = document->history.cursor == NULL
        ? document->history.head
        : document->history.cursor->next;
    canvas_document_touch(document);
    return true;
}

void canvas_document_replay(
    const CanvasDocument *document,
    CanvasReplayFn replay,
    void *userdata)
{
    if (document == NULL || replay == NULL || document->history.cursor == NULL) {
        return;
    }

    for (const CanvasOperation *operation = document->history.head;
         operation != NULL;
         operation = operation->next) {
        if (operation->type == CANVAS_OPERATION_DRAW_CELLS) {
            for (size_t i = 0; i < operation->sample_count; ++i) {
                replay(&operation->samples[i], userdata);
            }
        }
        if (operation == document->history.cursor) {
            break;
        }
    }
}

bool canvas_document_contains_output(
    const CanvasDocument *document,
    TgVec2i position)
{
    if (document == NULL ||
        document->output_size.w <= 0 ||
        document->output_size.h <= 0) {
        return false;
    }

    int min_x = -(document->output_size.w / 2);
    int min_y = -(document->output_size.h / 2);
    int64_t max_x = (int64_t)min_x + document->output_size.w;
    int64_t max_y = (int64_t)min_y + document->output_size.h;

    return position.x >= min_x &&
           position.y >= min_y &&
           (int64_t)position.x < max_x &&
           (int64_t)position.y < max_y;
}
