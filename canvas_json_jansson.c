#include "canvas_json_jansson.h"

#include "canvas_internal.h"

#include <jansson.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))
#define CANVAS_JANSSON_DEPTH_MARGIN 8u

static bool canvas_jansson_chain_has_cycle(const CanvasOperation *head)
{
    const CanvasOperation *slow = head;
    const CanvasOperation *fast = head;
    while (fast != NULL && fast->next != NULL) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) {
            return true;
        }
    }
    return false;
}

static TgResult canvas_jansson_validate_document(
    const CanvasDocument *document,
    size_t *out_count,
    size_t *out_cursor_index)
{
    if (document == NULL ||
        out_count == NULL ||
        out_cursor_index == NULL ||
        document->revision > CANVAS_DOCUMENT_MAX_REVISION ||
        document->output_size.w <= 0 ||
        document->output_size.h <= 0 ||
        canvas_jansson_chain_has_cycle(document->history.head)) {
        return TG_ERR_INVALID;
    }
    if (sizeof(json_int_t) < sizeof(int64_t)) {
        return TG_ERR_UNSUPPORTED;
    }
    if (document->history.head == NULL) {
        if (document->history.tail != NULL ||
            document->history.cursor != NULL ||
            document->history.operation_count != 0) {
            return TG_ERR_INVALID;
        }
        *out_count = 0;
        *out_cursor_index = SIZE_MAX;
        return TG_OK;
    }
    if (document->history.head->prev != NULL) {
        return TG_ERR_INVALID;
    }

    const CanvasOperation *previous = NULL;
    const CanvasOperation *operation = document->history.head;
    size_t count = 0;
    size_t cursor_index = SIZE_MAX;
    while (operation != NULL) {
        if (operation->prev != previous ||
            operation->type != CANVAS_OPERATION_DRAW_CELLS ||
            operation->samples == NULL ||
            operation->sample_count == 0 ||
            operation->sample_count >
                SIZE_MAX / sizeof(*operation->samples)) {
            return TG_ERR_INVALID;
        }
        if (operation->sample_count > (size_t)INT64_MAX) {
            return TG_ERR_UNSUPPORTED;
        }
        if (operation == document->history.cursor) {
            cursor_index = count;
        }
        if (count == SIZE_MAX) {
            return TG_ERR_INVALID;
        }
        ++count;
        previous = operation;
        operation = operation->next;
    }

    if (document->history.tail != previous ||
        document->history.operation_count != count ||
        (document->history.cursor != NULL && cursor_index == SIZE_MAX)) {
        return TG_ERR_INVALID;
    }
    if (count > (size_t)INT64_MAX) {
        return TG_ERR_UNSUPPORTED;
    }
    *out_count = count;
    *out_cursor_index = cursor_index;
    return TG_OK;
}

static json_t *canvas_jansson_integer_u64(uint64_t value)
{
    if (value > CANVAS_DOCUMENT_MAX_REVISION ||
        sizeof(json_int_t) < sizeof(int64_t)) {
        return NULL;
    }
    return json_integer((json_int_t)value);
}

static json_t *canvas_jansson_integer_size(size_t value)
{
    return value <= (size_t)INT64_MAX
        ? canvas_jansson_integer_u64((uint64_t)value)
        : NULL;
}

static bool canvas_jansson_add_named(
    json_t *object,
    const char *name,
    json_t *value)
{
    return value != NULL && json_object_set_new(object, name, value) == 0;
}

static bool canvas_jansson_add_array_item(json_t *array, json_t *value)
{
    return value != NULL && json_array_append_new(array, value) == 0;
}

static void canvas_jansson_free(void *pointer)
{
    json_malloc_t json_malloc = NULL;
    json_free_t json_free = NULL;
    json_get_alloc_funcs(&json_malloc, &json_free);
    (void)json_malloc;
    json_free(pointer);
}

static json_t *canvas_jansson_build_cell(const TuiCell *cell)
{
    json_t *object = json_object();
    json_t *characters = json_array();
    if (object == NULL || characters == NULL) {
        json_decref(characters);
        json_decref(object);
        return NULL;
    }
    for (size_t i = 0; i < sizeof(cell->ch); ++i) {
        if (!canvas_jansson_add_array_item(
                characters,
                canvas_jansson_integer_u64(
                    (unsigned char)cell->ch[i]))) {
            json_decref(characters);
            json_decref(object);
            return NULL;
        }
    }

    if (!canvas_jansson_add_named(object, "ch", characters) ||
        !canvas_jansson_add_named(
            object,
            "width",
            canvas_jansson_integer_u64(cell->width)) ||
        !canvas_jansson_add_named(
            object,
            "fg",
            canvas_jansson_integer_u64(cell->fg)) ||
        !canvas_jansson_add_named(
            object,
            "bg",
            canvas_jansson_integer_u64(cell->bg)) ||
        !canvas_jansson_add_named(
            object,
            "style",
            canvas_jansson_integer_u64(cell->style))) {
        json_decref(object);
        return NULL;
    }
    return object;
}

static json_t *canvas_jansson_build_sample(const CanvasSample *sample)
{
    json_t *object = json_object();
    json_t *position = json_object();
    if (object == NULL || position == NULL) {
        json_decref(position);
        json_decref(object);
        return NULL;
    }
    if (!canvas_jansson_add_named(
            position,
            "x",
            json_integer((json_int_t)sample->position.x)) ||
        !canvas_jansson_add_named(
            position,
            "y",
            json_integer((json_int_t)sample->position.y))) {
        json_decref(position);
        json_decref(object);
        return NULL;
    }
    if (!canvas_jansson_add_named(object, "position", position) ||
        !canvas_jansson_add_named(
            object,
            "cell",
            canvas_jansson_build_cell(&sample->cell))) {
        json_decref(object);
        return NULL;
    }
    return object;
}

static json_t *canvas_jansson_build_operation(
    const CanvasOperation *operation,
    size_t operation_index)
{
    if (operation == NULL) {
        return json_null();
    }

    json_t *object = json_object();
    json_t *samples = json_array();
    if (object == NULL || samples == NULL) {
        json_decref(samples);
        json_decref(object);
        return NULL;
    }
    for (size_t i = 0; i < operation->sample_count; ++i) {
        if (!canvas_jansson_add_array_item(
                samples,
                canvas_jansson_build_sample(&operation->samples[i]))) {
            json_decref(samples);
            json_decref(object);
            return NULL;
        }
    }

    if (!canvas_jansson_add_named(
            object,
            "type",
            canvas_jansson_integer_u64((uint64_t)operation->type)) ||
        !canvas_jansson_add_named(
            object,
            "prev",
            operation_index == 0
                ? json_null()
                : canvas_jansson_integer_size(operation_index - 1u))) {
        json_decref(samples);
        json_decref(object);
        return NULL;
    }
    if (!canvas_jansson_add_named(object, "samples", samples) ||
        !canvas_jansson_add_named(
            object,
            "sample_count",
            canvas_jansson_integer_size(operation->sample_count)) ||
        !canvas_jansson_add_named(
            object,
            "next",
            canvas_jansson_build_operation(
                operation->next,
                operation_index + 1u))) {
        json_decref(object);
        return NULL;
    }
    return object;
}

static json_t *canvas_jansson_build_document(
    const CanvasDocument *document,
    size_t operation_count,
    size_t cursor_index)
{
    json_t *root = json_object();
    json_t *output_size = json_object();
    if (root == NULL || output_size == NULL) {
        json_decref(output_size);
        json_decref(root);
        return NULL;
    }
    if (!canvas_jansson_add_named(
            output_size,
            "w",
            json_integer((json_int_t)document->output_size.w)) ||
        !canvas_jansson_add_named(
            output_size,
            "h",
            json_integer((json_int_t)document->output_size.h))) {
        json_decref(output_size);
        json_decref(root);
        return NULL;
    }
    if (!canvas_jansson_add_named(root, "output_size", output_size)) {
        json_decref(root);
        return NULL;
    }

    json_t *history = json_object();
    if (history == NULL) {
        json_decref(root);
        return NULL;
    }
    if (!canvas_jansson_add_named(
            history,
            "head",
            canvas_jansson_build_operation(document->history.head, 0)) ||
        !canvas_jansson_add_named(
            history,
            "tail",
            operation_count == 0
                ? json_null()
                : canvas_jansson_integer_size(operation_count - 1u)) ||
        !canvas_jansson_add_named(
            history,
            "cursor",
            cursor_index == SIZE_MAX
                ? json_null()
                : canvas_jansson_integer_size(cursor_index)) ||
        !canvas_jansson_add_named(
            history,
            "operation_count",
            canvas_jansson_integer_size(operation_count))) {
        json_decref(history);
        json_decref(root);
        return NULL;
    }
    if (!canvas_jansson_add_named(root, "history", history) ||
        !canvas_jansson_add_named(
            root,
            "revision",
            canvas_jansson_integer_u64(document->revision))) {
        json_decref(root);
        return NULL;
    }
    return root;
}

TgResult canvas_document_dump_json_jansson(
    const CanvasDocument *document,
    char **out_json)
{
    if (out_json == NULL) {
        return TG_ERR_INVALID;
    }
    *out_json = NULL;

    size_t operation_count = 0;
    size_t cursor_index = SIZE_MAX;
    TgResult result = canvas_jansson_validate_document(
        document,
        &operation_count,
        &cursor_index);
    if (tg_result_err(result)) {
        return result;
    }
    if (JSON_PARSER_MAX_DEPTH <= CANVAS_JANSSON_DEPTH_MARGIN ||
        operation_count >
            (size_t)JSON_PARSER_MAX_DEPTH - CANVAS_JANSSON_DEPTH_MARGIN) {
        return TG_ERR_UNSUPPORTED;
    }

    json_t *root = canvas_jansson_build_document(
        document,
        operation_count,
        cursor_index);
    if (root == NULL) {
        return TG_ERR_NOMEM;
    }
    char *printed = json_dumps(root, JSON_COMPACT | JSON_PRESERVE_ORDER);
    json_decref(root);
    if (printed == NULL) {
        return TG_ERR_NOMEM;
    }

    size_t length = strlen(printed);
    if (length > SIZE_MAX - 2u) {
        canvas_jansson_free(printed);
        return TG_ERR_NOMEM;
    }
    char *json = malloc(length + 2u);
    if (json == NULL) {
        canvas_jansson_free(printed);
        return TG_ERR_NOMEM;
    }
    memcpy(json, printed, length);
    json[length] = '\n';
    json[length + 1u] = '\0';
    canvas_jansson_free(printed);
    *out_json = json;
    return TG_OK;
}

static bool canvas_jansson_object_fields(
    const json_t *object,
    const char *const *names,
    size_t name_count,
    const json_t **out_values)
{
    if (!json_is_object(object) || json_object_size(object) != name_count) {
        return false;
    }
    for (size_t i = 0; i < name_count; ++i) {
        out_values[i] = json_object_get(object, names[i]);
        if (out_values[i] == NULL) {
            return false;
        }
    }
    return true;
}

static TgResult canvas_jansson_u64(
    const json_t *item,
    uint64_t maximum,
    uint64_t *out_value)
{
    if (!json_is_integer(item) || out_value == NULL) {
        return TG_ERR_INVALID;
    }
    json_int_t value = json_integer_value(item);
    if (value < 0 || (uint64_t)value > maximum) {
        return TG_ERR_INVALID;
    }
    *out_value = (uint64_t)value;
    return TG_OK;
}

static TgResult canvas_jansson_size(
    const json_t *item,
    size_t *out_value)
{
    uint64_t value = 0;
    TgResult result = canvas_jansson_u64(item, SIZE_MAX, &value);
    if (tg_result_ok(result)) {
        *out_value = (size_t)value;
    }
    return result;
}

static TgResult canvas_jansson_i32(
    const json_t *item,
    int32_t *out_value)
{
    if (!json_is_integer(item) || out_value == NULL) {
        return TG_ERR_INVALID;
    }
    json_int_t value = json_integer_value(item);
    if (value < INT32_MIN || value > INT32_MAX) {
        return TG_ERR_INVALID;
    }
    *out_value = (int32_t)value;
    return TG_OK;
}

static TgResult canvas_jansson_nullable_index(
    const json_t *item,
    size_t *out_index,
    bool *out_is_null)
{
    if (json_is_null(item)) {
        *out_index = SIZE_MAX;
        *out_is_null = true;
        return TG_OK;
    }
    *out_is_null = false;
    return canvas_jansson_size(item, out_index);
}

static TgResult canvas_jansson_decode_cell(
    const json_t *object,
    TuiCell *out_cell)
{
    static const char *const names[] = {
        "ch", "width", "fg", "bg", "style",
    };
    const json_t *values[ARRAY_COUNT(names)];
    if (!canvas_jansson_object_fields(
            object,
            names,
            ARRAY_COUNT(names),
            values) ||
        !json_is_array(values[0]) ||
        json_array_size(values[0]) != sizeof(out_cell->ch)) {
        return TG_ERR_INVALID;
    }

    memset(out_cell, 0, sizeof(*out_cell));
    for (size_t i = 0; i < sizeof(out_cell->ch); ++i) {
        uint64_t byte = 0;
        TgResult result = canvas_jansson_u64(
            json_array_get(values[0], i),
            UINT8_MAX,
            &byte);
        if (tg_result_err(result)) {
            return result;
        }
        out_cell->ch[i] = (char)(unsigned char)byte;
    }

    uint64_t width = 0;
    uint64_t fg = 0;
    uint64_t bg = 0;
    uint64_t style = 0;
    TgResult result = canvas_jansson_u64(values[1], UINT8_MAX, &width);
    if (tg_result_ok(result)) {
        result = canvas_jansson_u64(values[2], UINT32_MAX, &fg);
    }
    if (tg_result_ok(result)) {
        result = canvas_jansson_u64(values[3], UINT32_MAX, &bg);
    }
    if (tg_result_ok(result)) {
        result = canvas_jansson_u64(values[4], UINT16_MAX, &style);
    }
    if (tg_result_err(result)) {
        return result;
    }
    out_cell->width = (uint8_t)width;
    out_cell->fg = (uint32_t)fg;
    out_cell->bg = (uint32_t)bg;
    out_cell->style = (uint16_t)style;
    return TG_OK;
}

static TgResult canvas_jansson_decode_sample(
    const json_t *object,
    CanvasSample *out_sample)
{
    static const char *const sample_names[] = {"position", "cell"};
    static const char *const position_names[] = {"x", "y"};
    const json_t *sample_values[ARRAY_COUNT(sample_names)];
    const json_t *position_values[ARRAY_COUNT(position_names)];
    if (!canvas_jansson_object_fields(
            object,
            sample_names,
            ARRAY_COUNT(sample_names),
            sample_values) ||
        !canvas_jansson_object_fields(
            sample_values[0],
            position_names,
            ARRAY_COUNT(position_names),
            position_values)) {
        return TG_ERR_INVALID;
    }

    memset(out_sample, 0, sizeof(*out_sample));
    TgResult result = canvas_jansson_i32(
        position_values[0],
        &out_sample->position.x);
    if (tg_result_ok(result)) {
        result = canvas_jansson_i32(
            position_values[1],
            &out_sample->position.y);
    }
    if (tg_result_ok(result)) {
        result = canvas_jansson_decode_cell(
            sample_values[1],
            &out_sample->cell);
    }
    return result;
}

static TgResult canvas_jansson_decode_operations(
    const json_t *head,
    size_t declared_count,
    size_t tail_index,
    bool tail_is_null,
    size_t cursor_index,
    bool cursor_is_null,
    CanvasHistory *out_history)
{
    memset(out_history, 0, sizeof(*out_history));
    if (json_is_null(head)) {
        return declared_count == 0 && tail_is_null && cursor_is_null
            ? TG_OK
            : TG_ERR_INVALID;
    }
    if (!json_is_object(head) ||
        declared_count == 0 ||
        tail_is_null ||
        tail_index != declared_count - 1u ||
        (!cursor_is_null && cursor_index >= declared_count)) {
        return TG_ERR_INVALID;
    }

    static const char *const names[] = {
        "type", "prev", "next", "samples", "sample_count",
    };
    CanvasOperation *previous = NULL;
    const json_t *operation_object = head;
    size_t operation_index = 0;
    TgResult result = TG_OK;

    while (!json_is_null(operation_object)) {
        const json_t *values[ARRAY_COUNT(names)];
        if (!canvas_jansson_object_fields(
                operation_object,
                names,
                ARRAY_COUNT(names),
                values) ||
            operation_index >= declared_count) {
            result = TG_ERR_INVALID;
            break;
        }

        uint64_t type = 0;
        result = canvas_jansson_u64(
            values[0],
            CANVAS_OPERATION_DRAW_CELLS,
            &type);
        size_t previous_index = SIZE_MAX;
        bool previous_is_null = false;
        if (tg_result_ok(result)) {
            result = canvas_jansson_nullable_index(
                values[1],
                &previous_index,
                &previous_is_null);
        }
        if (tg_result_err(result)) {
            break;
        }
        if ((operation_index == 0
                ? !previous_is_null
                : previous_is_null ||
                    previous_index != operation_index - 1u) ||
            !json_is_array(values[3])) {
            result = TG_ERR_INVALID;
            break;
        }

        size_t sample_count = 0;
        result = canvas_jansson_size(values[4], &sample_count);
        if (tg_result_err(result)) {
            break;
        }
        if (sample_count == 0 ||
            sample_count != json_array_size(values[3]) ||
            sample_count > SIZE_MAX / sizeof(CanvasSample)) {
            result = TG_ERR_INVALID;
            break;
        }

        CanvasOperation *operation = calloc(1, sizeof(*operation));
        if (operation == NULL) {
            result = TG_ERR_NOMEM;
            break;
        }
        operation->samples = calloc(
            sample_count,
            sizeof(*operation->samples));
        if (operation->samples == NULL) {
            free(operation);
            result = TG_ERR_NOMEM;
            break;
        }
        operation->type = (CanvasOperationType)type;
        operation->sample_count = sample_count;
        operation->prev = previous;
        if (previous != NULL) {
            previous->next = operation;
        } else {
            out_history->head = operation;
        }
        previous = operation;
        out_history->tail = operation;
        if (!cursor_is_null && cursor_index == operation_index) {
            out_history->cursor = operation;
        }

        for (size_t i = 0; i < sample_count; ++i) {
            result = canvas_jansson_decode_sample(
                json_array_get(values[3], i),
                &operation->samples[i]);
            if (tg_result_err(result)) {
                break;
            }
        }
        if (tg_result_err(result)) {
            break;
        }

        operation_object = values[2];
        ++operation_index;
    }

    if (tg_result_ok(result) &&
        (operation_index != declared_count ||
         (!cursor_is_null && out_history->cursor == NULL))) {
        result = TG_ERR_INVALID;
    }
    if (tg_result_ok(result)) {
        out_history->operation_count = operation_index;
    }
    return result;
}

static TgResult canvas_jansson_decode_document(
    const json_t *root,
    CanvasDocument *out_document)
{
    static const char *const document_names[] = {
        "output_size", "history", "revision",
    };
    static const char *const size_names[] = {"w", "h"};
    static const char *const history_names[] = {
        "head", "tail", "cursor", "operation_count",
    };
    const json_t *document_values[ARRAY_COUNT(document_names)];
    const json_t *size_values[ARRAY_COUNT(size_names)];
    const json_t *history_values[ARRAY_COUNT(history_names)];
    CanvasDocument document;
    memset(&document, 0, sizeof(document));

    TgResult result = TG_OK;
    if (sizeof(json_int_t) < sizeof(int64_t)) {
        result = TG_ERR_UNSUPPORTED;
    } else if (!canvas_jansson_object_fields(
                   root,
                   document_names,
                   ARRAY_COUNT(document_names),
                   document_values) ||
               !canvas_jansson_object_fields(
                   document_values[0],
                   size_names,
                   ARRAY_COUNT(size_names),
                   size_values) ||
               !canvas_jansson_object_fields(
                   document_values[1],
                   history_names,
                   ARRAY_COUNT(history_names),
                   history_values)) {
        result = TG_ERR_INVALID;
    }
    if (tg_result_ok(result)) {
        result = canvas_jansson_i32(
            size_values[0],
            &document.output_size.w);
    }
    if (tg_result_ok(result)) {
        result = canvas_jansson_i32(
            size_values[1],
            &document.output_size.h);
    }
    if (tg_result_ok(result) &&
        (document.output_size.w <= 0 || document.output_size.h <= 0)) {
        result = TG_ERR_INVALID;
    }
    if (tg_result_ok(result)) {
        result = canvas_jansson_u64(
            document_values[2],
            CANVAS_DOCUMENT_MAX_REVISION,
            &document.revision);
    }

    size_t operation_count = 0;
    size_t tail_index = SIZE_MAX;
    size_t cursor_index = SIZE_MAX;
    bool tail_is_null = false;
    bool cursor_is_null = false;
    if (tg_result_ok(result)) {
        result = canvas_jansson_size(
            history_values[3],
            &operation_count);
    }
    if (tg_result_ok(result)) {
        result = canvas_jansson_nullable_index(
            history_values[1],
            &tail_index,
            &tail_is_null);
    }
    if (tg_result_ok(result)) {
        result = canvas_jansson_nullable_index(
            history_values[2],
            &cursor_index,
            &cursor_is_null);
    }
    if (tg_result_ok(result)) {
        result = canvas_jansson_decode_operations(
            history_values[0],
            operation_count,
            tail_index,
            tail_is_null,
            cursor_index,
            cursor_is_null,
            &document.history);
    }

    if (tg_result_err(result)) {
        canvas_document_destroy(&document);
        return result;
    }
    *out_document = document;
    return TG_OK;
}

static TgResult canvas_jansson_load_error(const json_error_t *error)
{
    if (error != NULL && json_error_code(error) == json_error_out_of_memory) {
        return TG_ERR_NOMEM;
    }
    if (error != NULL && json_error_code(error) == json_error_stack_overflow) {
        return TG_ERR_UNSUPPORTED;
    }
    return TG_ERR_INVALID;
}

TgResult canvas_document_load_json_jansson(
    const char *json,
    CanvasDocument *out_document)
{
    if (out_document == NULL) {
        return TG_ERR_INVALID;
    }
    memset(out_document, 0, sizeof(*out_document));
    if (json == NULL) {
        return TG_ERR_INVALID;
    }

    json_error_t error;
    json_t *root = json_loads(json, JSON_REJECT_DUPLICATES, &error);
    if (root == NULL) {
        return canvas_jansson_load_error(&error);
    }
    TgResult result = canvas_jansson_decode_document(root, out_document);
    json_decref(root);
    return result;
}

TgResult canvas_document_save_json_file_jansson(
    const CanvasDocument *document,
    const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return TG_ERR_INVALID;
    }

    char *json = NULL;
    TgResult result = canvas_document_dump_json_jansson(document, &json);
    if (tg_result_err(result)) {
        return result;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        free(json);
        return TG_ERR;
    }
    size_t length = strlen(json);
    bool wrote_all = fwrite(json, 1, length, file) == length;
    bool closed = fclose(file) == 0;
    free(json);
    return wrote_all && closed ? TG_OK : TG_ERR;
}

TgResult canvas_document_load_json_file_jansson(
    const char *path,
    CanvasDocument *out_document)
{
    if (path == NULL || path[0] == '\0' || out_document == NULL) {
        return TG_ERR_INVALID;
    }
    memset(out_document, 0, sizeof(*out_document));

    errno = 0;
    json_error_t error;
    json_t *root = json_load_file(path, JSON_REJECT_DUPLICATES, &error);
    if (root == NULL) {
        if (json_error_code(&error) == json_error_cannot_open_file) {
            return errno == ENOENT ? TG_ERR_NOT_FOUND : TG_ERR;
        }
        return canvas_jansson_load_error(&error);
    }
    TgResult result = canvas_jansson_decode_document(root, out_document);
    json_decref(root);
    return result;
}
