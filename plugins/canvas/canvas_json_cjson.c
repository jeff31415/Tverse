/* cJSON-backed Canvas document serialization. */
#include "canvas_json_cjson.h"

#include "canvas_internal.h"
#include "cJSON.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))
#define CANVAS_CJSON_SAFE_INTEGER 9007199254740991.0

static bool canvas_cjson_chain_has_cycle(const CanvasOperation *head)
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

static TgResult canvas_cjson_validate_document(
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
        canvas_cjson_chain_has_cycle(document->history.head)) {
        return TG_ERR_INVALID;
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
    *out_count = count;
    *out_cursor_index = cursor_index;
    return TG_OK;
}

static cJSON *canvas_cjson_raw_u64(uint64_t value)
{
    char text[32];
    int length = snprintf(text, sizeof(text), "%" PRIu64, value);
    return length > 0 && (size_t)length < sizeof(text)
        ? cJSON_CreateRaw(text)
        : NULL;
}

static cJSON *canvas_cjson_raw_size(size_t value)
{
    char text[32];
    int length = snprintf(text, sizeof(text), "%ju", (uintmax_t)value);
    return length > 0 && (size_t)length < sizeof(text)
        ? cJSON_CreateRaw(text)
        : NULL;
}

static cJSON *canvas_cjson_raw_i32(int32_t value)
{
    char text[32];
    int length = snprintf(text, sizeof(text), "%" PRId32, value);
    return length > 0 && (size_t)length < sizeof(text)
        ? cJSON_CreateRaw(text)
        : NULL;
}

static bool canvas_cjson_add_named(
    cJSON *object,
    const char *name,
    cJSON *item)
{
    if (item == NULL) {
        return false;
    }
    if (!cJSON_AddItemToObject(object, name, item)) {
        cJSON_Delete(item);
        return false;
    }
    return true;
}

static bool canvas_cjson_add_array_item(cJSON *array, cJSON *item)
{
    if (item == NULL) {
        return false;
    }
    if (!cJSON_AddItemToArray(array, item)) {
        cJSON_Delete(item);
        return false;
    }
    return true;
}

static cJSON *canvas_cjson_build_cell(const TuiCell *cell)
{
    cJSON *object = cJSON_CreateObject();
    cJSON *characters = cJSON_CreateArray();
    if (object == NULL || characters == NULL) {
        cJSON_Delete(object);
        cJSON_Delete(characters);
        return NULL;
    }
    for (size_t i = 0; i < sizeof(cell->ch); ++i) {
        if (!canvas_cjson_add_array_item(
                characters,
                canvas_cjson_raw_u64((unsigned char)cell->ch[i]))) {
            cJSON_Delete(object);
            cJSON_Delete(characters);
            return NULL;
        }
    }

    if (!canvas_cjson_add_named(object, "ch", characters) ||
        !canvas_cjson_add_named(
            object,
            "width",
            canvas_cjson_raw_u64(cell->width)) ||
        !canvas_cjson_add_named(
            object,
            "fg",
            canvas_cjson_raw_u64(cell->fg)) ||
        !canvas_cjson_add_named(
            object,
            "bg",
            canvas_cjson_raw_u64(cell->bg)) ||
        !canvas_cjson_add_named(
            object,
            "style",
            canvas_cjson_raw_u64(cell->style))) {
        cJSON_Delete(object);
        return NULL;
    }
    return object;
}

static cJSON *canvas_cjson_build_sample(const CanvasSample *sample)
{
    cJSON *object = cJSON_CreateObject();
    cJSON *position = cJSON_CreateObject();
    if (object == NULL || position == NULL) {
        cJSON_Delete(object);
        cJSON_Delete(position);
        return NULL;
    }
    if (!canvas_cjson_add_named(
            position,
            "x",
            canvas_cjson_raw_i32(sample->position.x)) ||
        !canvas_cjson_add_named(
            position,
            "y",
            canvas_cjson_raw_i32(sample->position.y))) {
        cJSON_Delete(position);
        cJSON_Delete(object);
        return NULL;
    }
    if (!canvas_cjson_add_named(object, "position", position) ||
        !canvas_cjson_add_named(
            object,
            "cell",
            canvas_cjson_build_cell(&sample->cell))) {
        cJSON_Delete(object);
        return NULL;
    }
    return object;
}

static cJSON *canvas_cjson_build_operation(
    const CanvasOperation *operation,
    size_t operation_index)
{
    if (operation == NULL) {
        return cJSON_CreateNull();
    }

    cJSON *object = cJSON_CreateObject();
    cJSON *samples = cJSON_CreateArray();
    if (object == NULL || samples == NULL) {
        cJSON_Delete(object);
        cJSON_Delete(samples);
        return NULL;
    }
    for (size_t i = 0; i < operation->sample_count; ++i) {
        if (!canvas_cjson_add_array_item(
                samples,
                canvas_cjson_build_sample(&operation->samples[i]))) {
            cJSON_Delete(object);
            cJSON_Delete(samples);
            return NULL;
        }
    }

    if (!canvas_cjson_add_named(
            object,
            "type",
            canvas_cjson_raw_u64((uint64_t)operation->type))) {
        cJSON_Delete(samples);
        cJSON_Delete(object);
        return NULL;
    }
    cJSON *previous = operation_index == 0
        ? cJSON_CreateNull()
        : canvas_cjson_raw_size(operation_index - 1u);
    if (!canvas_cjson_add_named(object, "prev", previous)) {
        cJSON_Delete(samples);
        cJSON_Delete(object);
        return NULL;
    }
    if (!canvas_cjson_add_named(object, "samples", samples) ||
        !canvas_cjson_add_named(
            object,
            "sample_count",
            canvas_cjson_raw_size(operation->sample_count)) ||
        !canvas_cjson_add_named(
            object,
            "next",
            canvas_cjson_build_operation(
                operation->next,
                operation_index + 1u))) {
        cJSON_Delete(object);
        return NULL;
    }
    return object;
}

static cJSON *canvas_cjson_build_document(
    const CanvasDocument *document,
    size_t operation_count,
    size_t cursor_index)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON *output_size = cJSON_CreateObject();
    if (output_size == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    if (!canvas_cjson_add_named(
            output_size,
            "w",
            canvas_cjson_raw_i32(document->output_size.w)) ||
        !canvas_cjson_add_named(
            output_size,
            "h",
            canvas_cjson_raw_i32(document->output_size.h))) {
        cJSON_Delete(output_size);
        cJSON_Delete(root);
        return NULL;
    }
    if (!canvas_cjson_add_named(root, "output_size", output_size)) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *history = cJSON_CreateObject();
    if (history == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    if (!canvas_cjson_add_named(
            history,
            "head",
            canvas_cjson_build_operation(document->history.head, 0)) ||
        !canvas_cjson_add_named(
            history,
            "tail",
            operation_count == 0
                ? cJSON_CreateNull()
                : canvas_cjson_raw_size(operation_count - 1u)) ||
        !canvas_cjson_add_named(
            history,
            "cursor",
            cursor_index == SIZE_MAX
                ? cJSON_CreateNull()
                : canvas_cjson_raw_size(cursor_index)) ||
        !canvas_cjson_add_named(
            history,
            "operation_count",
            canvas_cjson_raw_size(operation_count))) {
        cJSON_Delete(history);
        cJSON_Delete(root);
        return NULL;
    }
    if (!canvas_cjson_add_named(root, "history", history) ||
        !canvas_cjson_add_named(
            root,
            "revision",
            canvas_cjson_raw_u64(document->revision))) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

TgResult canvas_document_dump_json_cjson(
    const CanvasDocument *document,
    char **out_json)
{
    if (out_json == NULL) {
        return TG_ERR_INVALID;
    }
    *out_json = NULL;

    size_t operation_count = 0;
    size_t cursor_index = SIZE_MAX;
    TgResult result = canvas_cjson_validate_document(
        document,
        &operation_count,
        &cursor_index);
    if (tg_result_err(result)) {
        return result;
    }
    /* Leave room for root/history plus sample, cell and character objects. */
    if (operation_count > CJSON_NESTING_LIMIT - 8u) {
        return TG_ERR_UNSUPPORTED;
    }

    cJSON *root = canvas_cjson_build_document(
        document,
        operation_count,
        cursor_index);
    if (root == NULL) {
        return TG_ERR_NOMEM;
    }
    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (printed == NULL) {
        return TG_ERR_NOMEM;
    }

    size_t length = strlen(printed);
    if (length > SIZE_MAX - 2u) {
        cJSON_free(printed);
        return TG_ERR_NOMEM;
    }
    char *json = malloc(length + 2u);
    if (json == NULL) {
        cJSON_free(printed);
        return TG_ERR_NOMEM;
    }
    memcpy(json, printed, length);
    json[length] = '\n';
    json[length + 1u] = '\0';
    cJSON_free(printed);
    *out_json = json;
    return TG_OK;
}

static bool canvas_cjson_object_fields(
    const cJSON *object,
    const char *const *names,
    size_t name_count,
    const cJSON **out_values)
{
    if (!cJSON_IsObject(object)) {
        return false;
    }
    for (size_t i = 0; i < name_count; ++i) {
        out_values[i] = NULL;
    }

    size_t field_count = 0;
    for (const cJSON *field = object->child;
         field != NULL;
         field = field->next) {
        if (field_count == SIZE_MAX || field->string == NULL) {
            return false;
        }
        ++field_count;

        size_t matched = SIZE_MAX;
        for (size_t i = 0; i < name_count; ++i) {
            if (strcmp(field->string, names[i]) == 0) {
                matched = i;
                break;
            }
        }
        if (matched == SIZE_MAX || out_values[matched] != NULL) {
            return false;
        }
        out_values[matched] = field;
    }
    if (field_count != name_count) {
        return false;
    }
    for (size_t i = 0; i < name_count; ++i) {
        if (out_values[i] == NULL) {
            return false;
        }
    }
    return true;
}

static TgResult canvas_cjson_u64(
    const cJSON *item,
    uint64_t maximum,
    uint64_t *out_value)
{
    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        item->valuedouble < 0.0 ||
        floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble > (double)maximum) {
        return TG_ERR_INVALID;
    }
    if (item->valuedouble > CANVAS_CJSON_SAFE_INTEGER) {
        return TG_ERR_UNSUPPORTED;
    }
    *out_value = (uint64_t)item->valuedouble;
    return TG_OK;
}

static TgResult canvas_cjson_size(
    const cJSON *item,
    size_t *out_value)
{
    uint64_t value = 0;
    TgResult result = canvas_cjson_u64(item, SIZE_MAX, &value);
    if (tg_result_ok(result)) {
        *out_value = (size_t)value;
    }
    return result;
}

static TgResult canvas_cjson_i32(
    const cJSON *item,
    int32_t *out_value)
{
    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < INT32_MIN ||
        item->valuedouble > INT32_MAX) {
        return TG_ERR_INVALID;
    }
    *out_value = (int32_t)item->valuedouble;
    return TG_OK;
}

static TgResult canvas_cjson_nullable_index(
    const cJSON *item,
    size_t *out_index,
    bool *out_is_null)
{
    if (cJSON_IsNull(item)) {
        *out_index = SIZE_MAX;
        *out_is_null = true;
        return TG_OK;
    }
    *out_is_null = false;
    return canvas_cjson_size(item, out_index);
}

static TgResult canvas_cjson_decode_cell(
    const cJSON *object,
    TuiCell *out_cell)
{
    static const char *const names[] = {
        "ch", "width", "fg", "bg", "style",
    };
    const cJSON *values[ARRAY_COUNT(names)];
    if (!canvas_cjson_object_fields(
            object,
            names,
            ARRAY_COUNT(names),
            values) ||
        !cJSON_IsArray(values[0]) ||
        cJSON_GetArraySize(values[0]) != (int)sizeof(out_cell->ch)) {
        return TG_ERR_INVALID;
    }

    memset(out_cell, 0, sizeof(*out_cell));
    for (size_t i = 0; i < sizeof(out_cell->ch); ++i) {
        const cJSON *byte_item = cJSON_GetArrayItem(values[0], (int)i);
        uint64_t byte = 0;
        TgResult result = canvas_cjson_u64(byte_item, UINT8_MAX, &byte);
        if (tg_result_err(result)) {
            return result;
        }
        out_cell->ch[i] = (char)(unsigned char)byte;
    }

    uint64_t width = 0;
    uint64_t fg = 0;
    uint64_t bg = 0;
    uint64_t style = 0;
    TgResult result = canvas_cjson_u64(values[1], UINT8_MAX, &width);
    if (tg_result_ok(result)) {
        result = canvas_cjson_u64(values[2], UINT32_MAX, &fg);
    }
    if (tg_result_ok(result)) {
        result = canvas_cjson_u64(values[3], UINT32_MAX, &bg);
    }
    if (tg_result_ok(result)) {
        result = canvas_cjson_u64(values[4], UINT16_MAX, &style);
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

static TgResult canvas_cjson_decode_sample(
    const cJSON *object,
    CanvasSample *out_sample)
{
    static const char *const sample_names[] = {"position", "cell"};
    static const char *const position_names[] = {"x", "y"};
    const cJSON *sample_values[ARRAY_COUNT(sample_names)];
    const cJSON *position_values[ARRAY_COUNT(position_names)];
    if (!canvas_cjson_object_fields(
            object,
            sample_names,
            ARRAY_COUNT(sample_names),
            sample_values) ||
        !canvas_cjson_object_fields(
            sample_values[0],
            position_names,
            ARRAY_COUNT(position_names),
            position_values)) {
        return TG_ERR_INVALID;
    }

    memset(out_sample, 0, sizeof(*out_sample));
    TgResult result = canvas_cjson_i32(
        position_values[0],
        &out_sample->position.x);
    if (tg_result_ok(result)) {
        result = canvas_cjson_i32(
            position_values[1],
            &out_sample->position.y);
    }
    if (tg_result_ok(result)) {
        result = canvas_cjson_decode_cell(
            sample_values[1],
            &out_sample->cell);
    }
    return result;
}

static TgResult canvas_cjson_decode_operations(
    const cJSON *head,
    size_t declared_count,
    size_t tail_index,
    bool tail_is_null,
    size_t cursor_index,
    bool cursor_is_null,
    CanvasHistory *out_history)
{
    memset(out_history, 0, sizeof(*out_history));
    if (cJSON_IsNull(head)) {
        return declared_count == 0 && tail_is_null && cursor_is_null
            ? TG_OK
            : TG_ERR_INVALID;
    }
    if (!cJSON_IsObject(head) ||
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
    const cJSON *operation_object = head;
    size_t operation_index = 0;
    TgResult result = TG_OK;

    while (!cJSON_IsNull(operation_object)) {
        const cJSON *values[ARRAY_COUNT(names)];
        if (!cJSON_IsObject(operation_object) ||
            !canvas_cjson_object_fields(
                operation_object,
                names,
                ARRAY_COUNT(names),
                values) ||
            operation_index >= declared_count) {
            result = TG_ERR_INVALID;
            break;
        }

        uint64_t type = 0;
        result = canvas_cjson_u64(
            values[0],
            CANVAS_OPERATION_DRAW_CELLS,
            &type);
        size_t previous_index = SIZE_MAX;
        bool previous_is_null = false;
        if (tg_result_ok(result)) {
            result = canvas_cjson_nullable_index(
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
            !cJSON_IsArray(values[3])) {
            result = TG_ERR_INVALID;
            break;
        }

        size_t sample_count = 0;
        result = canvas_cjson_size(values[4], &sample_count);
        int array_size = cJSON_GetArraySize(values[3]);
        if (tg_result_err(result)) {
            break;
        }
        if (sample_count == 0 ||
            array_size < 0 ||
            sample_count != (size_t)array_size ||
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
            result = canvas_cjson_decode_sample(
                cJSON_GetArrayItem(values[3], (int)i),
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

TgResult canvas_document_load_json_cjson(
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

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithOpts(json, &parse_end, true);
    if (root == NULL || parse_end == NULL) {
        cJSON_Delete(root);
        return TG_ERR_INVALID;
    }

    static const char *const document_names[] = {
        "output_size", "history", "revision",
    };
    static const char *const size_names[] = {"w", "h"};
    static const char *const history_names[] = {
        "head", "tail", "cursor", "operation_count",
    };
    const cJSON *document_values[ARRAY_COUNT(document_names)];
    const cJSON *size_values[ARRAY_COUNT(size_names)];
    const cJSON *history_values[ARRAY_COUNT(history_names)];
    CanvasDocument document;
    memset(&document, 0, sizeof(document));

    TgResult result = TG_OK;
    if (!canvas_cjson_object_fields(
            root,
            document_names,
            ARRAY_COUNT(document_names),
            document_values) ||
        !canvas_cjson_object_fields(
            document_values[0],
            size_names,
            ARRAY_COUNT(size_names),
            size_values) ||
        !canvas_cjson_object_fields(
            document_values[1],
            history_names,
            ARRAY_COUNT(history_names),
            history_values)) {
        result = TG_ERR_INVALID;
    }
    if (tg_result_ok(result)) {
        result = canvas_cjson_i32(
            size_values[0],
            &document.output_size.w);
    }
    if (tg_result_ok(result)) {
        result = canvas_cjson_i32(
            size_values[1],
            &document.output_size.h);
    }
    if (tg_result_ok(result) &&
        (document.output_size.w <= 0 || document.output_size.h <= 0)) {
        result = TG_ERR_INVALID;
    }
    if (tg_result_ok(result)) {
        result = canvas_cjson_u64(
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
        result = canvas_cjson_size(
            history_values[3],
            &operation_count);
    }
    if (tg_result_ok(result)) {
        result = canvas_cjson_nullable_index(
            history_values[1],
            &tail_index,
            &tail_is_null);
    }
    if (tg_result_ok(result)) {
        result = canvas_cjson_nullable_index(
            history_values[2],
            &cursor_index,
            &cursor_is_null);
    }
    if (tg_result_ok(result)) {
        result = canvas_cjson_decode_operations(
            history_values[0],
            operation_count,
            tail_index,
            tail_is_null,
            cursor_index,
            cursor_is_null,
            &document.history);
    }

    cJSON_Delete(root);
    if (tg_result_err(result)) {
        canvas_document_destroy(&document);
        return result;
    }
    *out_document = document;
    return TG_OK;
}

TgResult canvas_document_save_json_file_cjson(
    const CanvasDocument *document,
    const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return TG_ERR_INVALID;
    }

    char *json = NULL;
    TgResult result = canvas_document_dump_json_cjson(document, &json);
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

TgResult canvas_document_load_json_file_cjson(
    const char *path,
    CanvasDocument *out_document)
{
    if (path == NULL || path[0] == '\0' || out_document == NULL) {
        return TG_ERR_INVALID;
    }
    memset(out_document, 0, sizeof(*out_document));

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? TG_ERR_NOT_FOUND : TG_ERR;
    }

    size_t capacity = 4096u;
    size_t length = 0;
    char *json = malloc(capacity);
    if (json == NULL) {
        (void)fclose(file);
        return TG_ERR_NOMEM;
    }

    TgResult result = TG_OK;
    for (;;) {
        if (length + 1u == capacity) {
            if (capacity > SIZE_MAX / 2u) {
                result = TG_ERR_NOMEM;
                break;
            }
            size_t new_capacity = capacity * 2u;
            char *new_json = realloc(json, new_capacity);
            if (new_json == NULL) {
                result = TG_ERR_NOMEM;
                break;
            }
            json = new_json;
            capacity = new_capacity;
        }
        size_t read_count = fread(
            json + length,
            1,
            capacity - length - 1u,
            file);
        length += read_count;
        if (read_count == 0) {
            if (ferror(file)) {
                result = TG_ERR;
            }
            break;
        }
    }
    if (fclose(file) != 0 && tg_result_ok(result)) {
        result = TG_ERR;
    }
    if (tg_result_ok(result)) {
        json[length] = '\0';
        result = canvas_document_load_json_cjson(json, out_document);
    }
    free(json);
    return result;
}
