#include "canvas_json.h"

#include "canvas_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CANVAS_JSON_MAX_DEPTH 4096u
#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

typedef enum CanvasJsonTokenType {
    CANVAS_JSON_OBJECT = 0,
    CANVAS_JSON_ARRAY,
    CANVAS_JSON_STRING,
    CANVAS_JSON_PRIMITIVE
} CanvasJsonTokenType;

typedef struct CanvasJsonToken {
    CanvasJsonTokenType type;
    size_t start;
    size_t end;
    size_t parent;
    size_t child_count;
} CanvasJsonToken;

typedef struct CanvasJsonParser {
    const char *source;
    size_t length;
    size_t position;
    CanvasJsonToken *tokens;
    size_t token_count;
    size_t token_capacity;
} CanvasJsonParser;

typedef struct CanvasJsonBuffer {
    char *data;
    size_t length;
    size_t capacity;
} CanvasJsonBuffer;

static bool canvas_json_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static bool canvas_json_is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static bool canvas_json_is_hex(char ch)
{
    return canvas_json_is_digit(ch) ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

static void canvas_json_skip_space(CanvasJsonParser *parser)
{
    while (parser->position < parser->length &&
           canvas_json_is_space(parser->source[parser->position])) {
        ++parser->position;
    }
}

static TgResult canvas_json_reserve_tokens(CanvasJsonParser *parser)
{
    if (parser->token_count < parser->token_capacity) {
        return TG_OK;
    }

    size_t capacity = parser->token_capacity == 0
        ? 128u
        : parser->token_capacity;
    if (capacity > SIZE_MAX / 2u) {
        return TG_ERR_NOMEM;
    }
    capacity *= 2u;
    if (capacity > SIZE_MAX / sizeof(*parser->tokens)) {
        return TG_ERR_NOMEM;
    }

    CanvasJsonToken *tokens = realloc(
        parser->tokens,
        capacity * sizeof(*parser->tokens));
    if (tokens == NULL) {
        return TG_ERR_NOMEM;
    }
    parser->tokens = tokens;
    parser->token_capacity = capacity;
    return TG_OK;
}

static TgResult canvas_json_add_token(
    CanvasJsonParser *parser,
    CanvasJsonTokenType type,
    size_t start,
    size_t end,
    size_t parent,
    size_t *out_index)
{
    TgResult result = canvas_json_reserve_tokens(parser);
    if (tg_result_err(result)) {
        return result;
    }

    size_t index = parser->token_count++;
    parser->tokens[index] = (CanvasJsonToken){
        .type = type,
        .start = start,
        .end = end,
        .parent = parent,
        .child_count = 0,
    };
    if (parent != SIZE_MAX) {
        if (parent >= index ||
            parser->tokens[parent].child_count == SIZE_MAX) {
            return TG_ERR_INVALID;
        }
        ++parser->tokens[parent].child_count;
    }
    *out_index = index;
    return TG_OK;
}

static TgResult canvas_json_parse_value(
    CanvasJsonParser *parser,
    size_t parent,
    size_t depth,
    size_t *out_index);

static TgResult canvas_json_parse_string(
    CanvasJsonParser *parser,
    size_t parent,
    size_t *out_index)
{
    if (parser->position >= parser->length ||
        parser->source[parser->position] != '"') {
        return TG_ERR_INVALID;
    }

    size_t start = ++parser->position;
    while (parser->position < parser->length) {
        unsigned char ch =
            (unsigned char)parser->source[parser->position++];
        if (ch == '"') {
            return canvas_json_add_token(
                parser,
                CANVAS_JSON_STRING,
                start,
                parser->position - 1u,
                parent,
                out_index);
        }
        if (ch < 0x20u) {
            return TG_ERR_INVALID;
        }
        if (ch != '\\') {
            continue;
        }
        if (parser->position >= parser->length) {
            return TG_ERR_INVALID;
        }
        char escaped = parser->source[parser->position++];
        if (escaped == 'u') {
            if (parser->length - parser->position < 4u) {
                return TG_ERR_INVALID;
            }
            for (size_t i = 0; i < 4u; ++i) {
                if (!canvas_json_is_hex(
                        parser->source[parser->position + i])) {
                    return TG_ERR_INVALID;
                }
            }
            parser->position += 4u;
        } else if (escaped != '"' && escaped != '\\' && escaped != '/' &&
                   escaped != 'b' && escaped != 'f' && escaped != 'n' &&
                   escaped != 'r' && escaped != 't') {
            return TG_ERR_INVALID;
        }
    }
    return TG_ERR_INVALID;
}

static TgResult canvas_json_parse_number(
    CanvasJsonParser *parser,
    size_t parent,
    size_t *out_index)
{
    size_t start = parser->position;
    if (parser->source[parser->position] == '-') {
        ++parser->position;
        if (parser->position >= parser->length) {
            return TG_ERR_INVALID;
        }
    }

    if (parser->source[parser->position] == '0') {
        ++parser->position;
    } else if (parser->source[parser->position] >= '1' &&
               parser->source[parser->position] <= '9') {
        do {
            ++parser->position;
        } while (parser->position < parser->length &&
                 canvas_json_is_digit(parser->source[parser->position]));
    } else {
        return TG_ERR_INVALID;
    }

    if (parser->position < parser->length &&
        parser->source[parser->position] == '.') {
        ++parser->position;
        size_t fraction_start = parser->position;
        while (parser->position < parser->length &&
               canvas_json_is_digit(parser->source[parser->position])) {
            ++parser->position;
        }
        if (parser->position == fraction_start) {
            return TG_ERR_INVALID;
        }
    }

    if (parser->position < parser->length &&
        (parser->source[parser->position] == 'e' ||
         parser->source[parser->position] == 'E')) {
        ++parser->position;
        if (parser->position < parser->length &&
            (parser->source[parser->position] == '+' ||
             parser->source[parser->position] == '-')) {
            ++parser->position;
        }
        size_t exponent_start = parser->position;
        while (parser->position < parser->length &&
               canvas_json_is_digit(parser->source[parser->position])) {
            ++parser->position;
        }
        if (parser->position == exponent_start) {
            return TG_ERR_INVALID;
        }
    }

    return canvas_json_add_token(
        parser,
        CANVAS_JSON_PRIMITIVE,
        start,
        parser->position,
        parent,
        out_index);
}

static bool canvas_json_match_literal(
    const CanvasJsonParser *parser,
    const char *literal,
    size_t literal_length)
{
    return parser->length - parser->position >= literal_length &&
           memcmp(
               parser->source + parser->position,
               literal,
               literal_length) == 0;
}

static TgResult canvas_json_parse_literal(
    CanvasJsonParser *parser,
    size_t parent,
    size_t *out_index)
{
    size_t literal_length = 0;
    if (canvas_json_match_literal(parser, "null", 4u) ||
        canvas_json_match_literal(parser, "true", 4u)) {
        literal_length = 4u;
    } else if (canvas_json_match_literal(parser, "false", 5u)) {
        literal_length = 5u;
    } else {
        return TG_ERR_INVALID;
    }

    size_t start = parser->position;
    parser->position += literal_length;
    return canvas_json_add_token(
        parser,
        CANVAS_JSON_PRIMITIVE,
        start,
        parser->position,
        parent,
        out_index);
}

static TgResult canvas_json_parse_object(
    CanvasJsonParser *parser,
    size_t parent,
    size_t depth,
    size_t *out_index)
{
    size_t object_index = SIZE_MAX;
    TgResult result = canvas_json_add_token(
        parser,
        CANVAS_JSON_OBJECT,
        parser->position,
        0,
        parent,
        &object_index);
    if (tg_result_err(result)) {
        return result;
    }
    ++parser->position;
    canvas_json_skip_space(parser);

    if (parser->position < parser->length &&
        parser->source[parser->position] == '}') {
        parser->tokens[object_index].end = ++parser->position;
        *out_index = object_index;
        return TG_OK;
    }

    for (;;) {
        size_t child_index = SIZE_MAX;
        result = canvas_json_parse_string(
            parser,
            object_index,
            &child_index);
        if (tg_result_err(result)) {
            return result;
        }
        canvas_json_skip_space(parser);
        if (parser->position >= parser->length ||
            parser->source[parser->position] != ':') {
            return TG_ERR_INVALID;
        }
        ++parser->position;
        canvas_json_skip_space(parser);
        result = canvas_json_parse_value(
            parser,
            object_index,
            depth + 1u,
            &child_index);
        if (tg_result_err(result)) {
            return result;
        }
        canvas_json_skip_space(parser);
        if (parser->position >= parser->length) {
            return TG_ERR_INVALID;
        }
        char delimiter = parser->source[parser->position++];
        if (delimiter == '}') {
            parser->tokens[object_index].end = parser->position;
            *out_index = object_index;
            return TG_OK;
        }
        if (delimiter != ',') {
            return TG_ERR_INVALID;
        }
        canvas_json_skip_space(parser);
    }
}

static TgResult canvas_json_parse_array(
    CanvasJsonParser *parser,
    size_t parent,
    size_t depth,
    size_t *out_index)
{
    size_t array_index = SIZE_MAX;
    TgResult result = canvas_json_add_token(
        parser,
        CANVAS_JSON_ARRAY,
        parser->position,
        0,
        parent,
        &array_index);
    if (tg_result_err(result)) {
        return result;
    }
    ++parser->position;
    canvas_json_skip_space(parser);

    if (parser->position < parser->length &&
        parser->source[parser->position] == ']') {
        parser->tokens[array_index].end = ++parser->position;
        *out_index = array_index;
        return TG_OK;
    }

    for (;;) {
        size_t child_index = SIZE_MAX;
        result = canvas_json_parse_value(
            parser,
            array_index,
            depth + 1u,
            &child_index);
        if (tg_result_err(result)) {
            return result;
        }
        canvas_json_skip_space(parser);
        if (parser->position >= parser->length) {
            return TG_ERR_INVALID;
        }
        char delimiter = parser->source[parser->position++];
        if (delimiter == ']') {
            parser->tokens[array_index].end = parser->position;
            *out_index = array_index;
            return TG_OK;
        }
        if (delimiter != ',') {
            return TG_ERR_INVALID;
        }
        canvas_json_skip_space(parser);
    }
}

static TgResult canvas_json_parse_value(
    CanvasJsonParser *parser,
    size_t parent,
    size_t depth,
    size_t *out_index)
{
    if (depth > CANVAS_JSON_MAX_DEPTH) {
        return TG_ERR_INVALID;
    }
    canvas_json_skip_space(parser);
    if (parser->position >= parser->length) {
        return TG_ERR_INVALID;
    }

    char ch = parser->source[parser->position];
    if (ch == '{') {
        return canvas_json_parse_object(parser, parent, depth, out_index);
    }
    if (ch == '[') {
        return canvas_json_parse_array(parser, parent, depth, out_index);
    }
    if (ch == '"') {
        return canvas_json_parse_string(parser, parent, out_index);
    }
    if (ch == '-' || canvas_json_is_digit(ch)) {
        return canvas_json_parse_number(parser, parent, out_index);
    }
    return canvas_json_parse_literal(parser, parent, out_index);
}

static TgResult canvas_json_parse(CanvasJsonParser *parser, const char *json)
{
    memset(parser, 0, sizeof(*parser));
    if (json == NULL) {
        return TG_ERR_INVALID;
    }
    parser->source = json;
    parser->length = strlen(json);

    size_t root_index = SIZE_MAX;
    TgResult result = canvas_json_parse_value(
        parser,
        SIZE_MAX,
        0,
        &root_index);
    if (tg_result_err(result)) {
        return result;
    }
    canvas_json_skip_space(parser);
    if (root_index != 0 ||
        parser->position != parser->length ||
        parser->token_count == 0) {
        return TG_ERR_INVALID;
    }
    return TG_OK;
}

static bool canvas_json_token_equals(
    const CanvasJsonParser *parser,
    size_t token_index,
    const char *text)
{
    if (token_index >= parser->token_count) {
        return false;
    }
    const CanvasJsonToken *token = &parser->tokens[token_index];
    size_t text_length = strlen(text);
    return token->end - token->start == text_length &&
           memcmp(parser->source + token->start, text, text_length) == 0;
}

static size_t canvas_json_token_after(
    const CanvasJsonParser *parser,
    size_t token_index)
{
    size_t index = token_index + 1u;
    size_t end = parser->tokens[token_index].end;
    while (index < parser->token_count &&
           parser->tokens[index].start < end) {
        ++index;
    }
    return index;
}

static bool canvas_json_object_fields(
    const CanvasJsonParser *parser,
    size_t object_index,
    const char *const *names,
    size_t name_count,
    size_t *out_values)
{
    if (object_index >= parser->token_count ||
        parser->tokens[object_index].type != CANVAS_JSON_OBJECT ||
        name_count > SIZE_MAX / 2u ||
        parser->tokens[object_index].child_count != name_count * 2u) {
        return false;
    }
    for (size_t i = 0; i < name_count; ++i) {
        out_values[i] = SIZE_MAX;
    }

    size_t key_index = object_index + 1u;
    for (size_t member = 0; member < name_count; ++member) {
        if (key_index >= parser->token_count ||
            parser->tokens[key_index].parent != object_index ||
            parser->tokens[key_index].type != CANVAS_JSON_STRING) {
            return false;
        }
        size_t value_index = key_index + 1u;
        if (value_index >= parser->token_count ||
            parser->tokens[value_index].parent != object_index) {
            return false;
        }

        size_t matched = SIZE_MAX;
        for (size_t i = 0; i < name_count; ++i) {
            if (canvas_json_token_equals(parser, key_index, names[i])) {
                matched = i;
                break;
            }
        }
        if (matched == SIZE_MAX || out_values[matched] != SIZE_MAX) {
            return false;
        }
        out_values[matched] = value_index;
        key_index = canvas_json_token_after(parser, value_index);
    }
    return true;
}

static bool canvas_json_u64(
    const CanvasJsonParser *parser,
    size_t token_index,
    uint64_t *out_value)
{
    if (token_index >= parser->token_count || out_value == NULL) {
        return false;
    }
    const CanvasJsonToken *token = &parser->tokens[token_index];
    if (token->type != CANVAS_JSON_PRIMITIVE ||
        token->start == token->end) {
        return false;
    }

    uint64_t value = 0;
    for (size_t position = token->start;
         position < token->end;
         ++position) {
        char ch = parser->source[position];
        if (!canvas_json_is_digit(ch)) {
            return false;
        }
        unsigned digit = (unsigned)(ch - '0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
    }
    *out_value = value;
    return true;
}

static bool canvas_json_size(
    const CanvasJsonParser *parser,
    size_t token_index,
    size_t *out_value)
{
    uint64_t value = 0;
    if (!canvas_json_u64(parser, token_index, &value) ||
        value > SIZE_MAX) {
        return false;
    }
    *out_value = (size_t)value;
    return true;
}

static bool canvas_json_i32(
    const CanvasJsonParser *parser,
    size_t token_index,
    int32_t *out_value)
{
    if (token_index >= parser->token_count || out_value == NULL) {
        return false;
    }
    const CanvasJsonToken *token = &parser->tokens[token_index];
    if (token->type != CANVAS_JSON_PRIMITIVE ||
        token->start == token->end) {
        return false;
    }

    size_t position = token->start;
    bool negative = parser->source[position] == '-';
    if (negative && ++position == token->end) {
        return false;
    }
    uint64_t magnitude = 0;
    for (; position < token->end; ++position) {
        char ch = parser->source[position];
        if (!canvas_json_is_digit(ch)) {
            return false;
        }
        unsigned digit = (unsigned)(ch - '0');
        if (magnitude > (UINT64_MAX - digit) / 10u) {
            return false;
        }
        magnitude = magnitude * 10u + digit;
    }

    if (negative) {
        if (magnitude > (uint64_t)INT32_MAX + 1u) {
            return false;
        }
        *out_value = magnitude == (uint64_t)INT32_MAX + 1u
            ? INT32_MIN
            : -(int32_t)magnitude;
    } else {
        if (magnitude > INT32_MAX) {
            return false;
        }
        *out_value = (int32_t)magnitude;
    }
    return true;
}

static bool canvas_json_null(
    const CanvasJsonParser *parser,
    size_t token_index)
{
    return token_index < parser->token_count &&
           parser->tokens[token_index].type == CANVAS_JSON_PRIMITIVE &&
           canvas_json_token_equals(parser, token_index, "null");
}

static TgResult canvas_json_buffer_reserve(
    CanvasJsonBuffer *buffer,
    size_t additional)
{
    if (additional > SIZE_MAX - buffer->length - 1u) {
        return TG_ERR_NOMEM;
    }
    size_t required = buffer->length + additional + 1u;
    if (required <= buffer->capacity) {
        return TG_OK;
    }

    size_t capacity = buffer->capacity == 0 ? 512u : buffer->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }
    char *data = realloc(buffer->data, capacity);
    if (data == NULL) {
        return TG_ERR_NOMEM;
    }
    buffer->data = data;
    buffer->capacity = capacity;
    return TG_OK;
}

static TgResult canvas_json_buffer_append_n(
    CanvasJsonBuffer *buffer,
    const char *text,
    size_t length)
{
    TgResult result = canvas_json_buffer_reserve(buffer, length);
    if (tg_result_err(result)) {
        return result;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return TG_OK;
}

static TgResult canvas_json_buffer_append(
    CanvasJsonBuffer *buffer,
    const char *text)
{
    return canvas_json_buffer_append_n(buffer, text, strlen(text));
}

static TgResult canvas_json_buffer_u64(
    CanvasJsonBuffer *buffer,
    uint64_t value)
{
    char text[32];
    int length = snprintf(text, sizeof(text), "%" PRIu64, value);
    return length > 0 && (size_t)length < sizeof(text)
        ? canvas_json_buffer_append_n(buffer, text, (size_t)length)
        : TG_ERR;
}

static TgResult canvas_json_buffer_size(
    CanvasJsonBuffer *buffer,
    size_t value)
{
    char text[32];
    int length = snprintf(text, sizeof(text), "%ju", (uintmax_t)value);
    return length > 0 && (size_t)length < sizeof(text)
        ? canvas_json_buffer_append_n(buffer, text, (size_t)length)
        : TG_ERR;
}

static TgResult canvas_json_buffer_i32(
    CanvasJsonBuffer *buffer,
    int32_t value)
{
    char text[32];
    int length = snprintf(text, sizeof(text), "%" PRId32, value);
    return length > 0 && (size_t)length < sizeof(text)
        ? canvas_json_buffer_append_n(buffer, text, (size_t)length)
        : TG_ERR;
}

static bool canvas_document_json_chain_has_cycle(
    const CanvasOperation *head)
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

static TgResult canvas_document_json_validate(
    const CanvasDocument *document,
    size_t *out_operation_count,
    size_t *out_cursor_index)
{
    if (document == NULL ||
        out_operation_count == NULL ||
        out_cursor_index == NULL ||
        document->revision > CANVAS_DOCUMENT_MAX_REVISION ||
        document->output_size.w <= 0 ||
        document->output_size.h <= 0 ||
        canvas_document_json_chain_has_cycle(document->history.head)) {
        return TG_ERR_INVALID;
    }

    if (document->history.head == NULL) {
        if (document->history.tail != NULL ||
            document->history.cursor != NULL ||
            document->history.operation_count != 0) {
            return TG_ERR_INVALID;
        }
        *out_operation_count = 0;
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
    *out_operation_count = count;
    *out_cursor_index = cursor_index;
    return TG_OK;
}

static TgResult canvas_json_dump_cell(
    CanvasJsonBuffer *buffer,
    const TuiCell *cell)
{
    TgResult result = canvas_json_buffer_append(buffer, "{\"ch\":[");
    for (size_t i = 0; tg_result_ok(result) && i < sizeof(cell->ch); ++i) {
        if (i > 0) {
            result = canvas_json_buffer_append(buffer, ",");
        }
        if (tg_result_ok(result)) {
            result = canvas_json_buffer_u64(
                buffer,
                (unsigned char)cell->ch[i]);
        }
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, "],\"width\":");
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_u64(buffer, cell->width);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, ",\"fg\":");
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_u64(buffer, cell->fg);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, ",\"bg\":");
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_u64(buffer, cell->bg);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, ",\"style\":");
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_u64(buffer, cell->style);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, "}");
    }
    return result;
}

static TgResult canvas_json_dump_sample(
    CanvasJsonBuffer *buffer,
    const CanvasSample *sample)
{
    TgResult result = canvas_json_buffer_append(
        buffer,
        "{\"position\":{\"x\":");
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_i32(buffer, sample->position.x);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, ",\"y\":");
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_i32(buffer, sample->position.y);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, "},\"cell\":");
    }
    if (tg_result_ok(result)) {
        result = canvas_json_dump_cell(buffer, &sample->cell);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, "}");
    }
    return result;
}

static TgResult canvas_json_dump_operation_prefix(
    CanvasJsonBuffer *buffer,
    const CanvasOperation *operation,
    size_t operation_index)
{
    TgResult result = canvas_json_buffer_append(buffer, "{\"type\":");
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_u64(buffer, (uint64_t)operation->type);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, ",\"prev\":");
    }
    if (tg_result_ok(result)) {
        result = operation_index == 0
            ? canvas_json_buffer_append(buffer, "null")
            : canvas_json_buffer_size(buffer, operation_index - 1u);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, ",\"samples\":[");
    }
    for (size_t i = 0;
         tg_result_ok(result) && i < operation->sample_count;
         ++i) {
        if (i > 0) {
            result = canvas_json_buffer_append(buffer, ",");
        }
        if (tg_result_ok(result)) {
            result = canvas_json_dump_sample(buffer, &operation->samples[i]);
        }
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, "],\"sample_count\":");
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_size(buffer, operation->sample_count);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(buffer, ",\"next\":");
    }
    return result;
}

TgResult canvas_document_dump_json(
    const CanvasDocument *document,
    char **out_json)
{
    if (out_json == NULL) {
        return TG_ERR_INVALID;
    }
    *out_json = NULL;

    size_t operation_count = 0;
    size_t cursor_index = SIZE_MAX;
    TgResult result = canvas_document_json_validate(
        document,
        &operation_count,
        &cursor_index);
    if (tg_result_err(result)) {
        return result;
    }

    CanvasJsonBuffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    result = canvas_json_buffer_append(
        &buffer,
        "{\"output_size\":{\"w\":");
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_i32(&buffer, document->output_size.w);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(&buffer, ",\"h\":");
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_i32(&buffer, document->output_size.h);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(
            &buffer,
            "},\"history\":{\"head\":");
    }

    const CanvasOperation *operation = document->history.head;
    size_t operation_index = 0;
    if (tg_result_ok(result) && operation == NULL) {
        result = canvas_json_buffer_append(&buffer, "null");
    }
    while (tg_result_ok(result) && operation != NULL) {
        result = canvas_json_dump_operation_prefix(
            &buffer,
            operation,
            operation_index);
        operation = operation->next;
        ++operation_index;
    }
    if (tg_result_ok(result) && operation_count > 0) {
        result = canvas_json_buffer_append(&buffer, "null");
        for (size_t i = 0; tg_result_ok(result) && i < operation_count; ++i) {
            result = canvas_json_buffer_append(&buffer, "}");
        }
    }

    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(&buffer, ",\"tail\":");
    }
    if (tg_result_ok(result)) {
        result = operation_count == 0
            ? canvas_json_buffer_append(&buffer, "null")
            : canvas_json_buffer_size(&buffer, operation_count - 1u);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(&buffer, ",\"cursor\":");
    }
    if (tg_result_ok(result)) {
        result = cursor_index == SIZE_MAX
            ? canvas_json_buffer_append(&buffer, "null")
            : canvas_json_buffer_size(&buffer, cursor_index);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(
            &buffer,
            ",\"operation_count\":");
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_size(&buffer, operation_count);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(&buffer, "},\"revision\":");
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_u64(&buffer, document->revision);
    }
    if (tg_result_ok(result)) {
        result = canvas_json_buffer_append(&buffer, "}\n");
    }

    if (tg_result_err(result)) {
        free(buffer.data);
        return result;
    }
    *out_json = buffer.data;
    return TG_OK;
}

static bool canvas_json_decode_cell(
    const CanvasJsonParser *parser,
    size_t token_index,
    TuiCell *out_cell)
{
    static const char *const names[] = {
        "ch", "width", "fg", "bg", "style",
    };
    size_t values[ARRAY_COUNT(names)];
    if (!canvas_json_object_fields(
            parser,
            token_index,
            names,
            ARRAY_COUNT(names),
            values)) {
        return false;
    }

    const CanvasJsonToken *characters = &parser->tokens[values[0]];
    if (characters->type != CANVAS_JSON_ARRAY ||
        characters->child_count != sizeof(out_cell->ch)) {
        return false;
    }

    memset(out_cell, 0, sizeof(*out_cell));
    size_t character_index = values[0] + 1u;
    for (size_t i = 0; i < sizeof(out_cell->ch); ++i) {
        uint64_t byte = 0;
        if (character_index >= parser->token_count ||
            parser->tokens[character_index].parent != values[0] ||
            !canvas_json_u64(parser, character_index, &byte) ||
            byte > UINT8_MAX) {
            return false;
        }
        out_cell->ch[i] = (char)(unsigned char)byte;
        character_index = canvas_json_token_after(parser, character_index);
    }

    uint64_t width = 0;
    uint64_t fg = 0;
    uint64_t bg = 0;
    uint64_t style = 0;
    if (!canvas_json_u64(parser, values[1], &width) ||
        width > UINT8_MAX ||
        !canvas_json_u64(parser, values[2], &fg) ||
        fg > UINT32_MAX ||
        !canvas_json_u64(parser, values[3], &bg) ||
        bg > UINT32_MAX ||
        !canvas_json_u64(parser, values[4], &style) ||
        style > UINT16_MAX) {
        return false;
    }
    out_cell->width = (uint8_t)width;
    out_cell->fg = (uint32_t)fg;
    out_cell->bg = (uint32_t)bg;
    out_cell->style = (uint16_t)style;
    return true;
}

static bool canvas_json_decode_sample(
    const CanvasJsonParser *parser,
    size_t token_index,
    CanvasSample *out_sample)
{
    static const char *const sample_names[] = {"position", "cell"};
    static const char *const position_names[] = {"x", "y"};
    size_t sample_values[ARRAY_COUNT(sample_names)];
    size_t position_values[ARRAY_COUNT(position_names)];
    if (!canvas_json_object_fields(
            parser,
            token_index,
            sample_names,
            ARRAY_COUNT(sample_names),
            sample_values) ||
        !canvas_json_object_fields(
            parser,
            sample_values[0],
            position_names,
            ARRAY_COUNT(position_names),
            position_values)) {
        return false;
    }

    memset(out_sample, 0, sizeof(*out_sample));
    return canvas_json_i32(
               parser,
               position_values[0],
               &out_sample->position.x) &&
           canvas_json_i32(
               parser,
               position_values[1],
               &out_sample->position.y) &&
           canvas_json_decode_cell(
               parser,
               sample_values[1],
               &out_sample->cell);
}

static bool canvas_json_nullable_index(
    const CanvasJsonParser *parser,
    size_t token_index,
    size_t *out_index,
    bool *out_is_null)
{
    if (canvas_json_null(parser, token_index)) {
        *out_index = SIZE_MAX;
        *out_is_null = true;
        return true;
    }
    *out_is_null = false;
    return canvas_json_size(parser, token_index, out_index);
}

static TgResult canvas_json_decode_operations(
    const CanvasJsonParser *parser,
    size_t head_token,
    size_t declared_count,
    size_t tail_index,
    bool tail_is_null,
    size_t cursor_index,
    bool cursor_is_null,
    CanvasHistory *out_history)
{
    memset(out_history, 0, sizeof(*out_history));
    if (canvas_json_null(parser, head_token)) {
        return declared_count == 0 && tail_is_null && cursor_is_null
            ? TG_OK
            : TG_ERR_INVALID;
    }
    if (parser->tokens[head_token].type != CANVAS_JSON_OBJECT ||
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
    size_t operation_token = head_token;
    size_t operation_index = 0;
    TgResult result = TG_OK;

    while (!canvas_json_null(parser, operation_token)) {
        size_t values[ARRAY_COUNT(names)];
        uint64_t type = 0;
        size_t previous_index = SIZE_MAX;
        bool previous_is_null = false;
        size_t sample_count = 0;
        if (operation_token >= parser->token_count ||
            parser->tokens[operation_token].type != CANVAS_JSON_OBJECT ||
            !canvas_json_object_fields(
                parser,
                operation_token,
                names,
                ARRAY_COUNT(names),
                values) ||
            !canvas_json_u64(parser, values[0], &type) ||
            type != CANVAS_OPERATION_DRAW_CELLS ||
            !canvas_json_nullable_index(
                parser,
                values[1],
                &previous_index,
                &previous_is_null) ||
            (operation_index == 0
                ? !previous_is_null
                : previous_is_null ||
                    previous_index != operation_index - 1u) ||
            parser->tokens[values[3]].type != CANVAS_JSON_ARRAY ||
            !canvas_json_size(parser, values[4], &sample_count) ||
            sample_count == 0 ||
            sample_count != parser->tokens[values[3]].child_count ||
            sample_count > SIZE_MAX / sizeof(CanvasSample) ||
            operation_index >= declared_count) {
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
        if (!cursor_is_null && operation_index == cursor_index) {
            out_history->cursor = operation;
        }

        size_t sample_token = values[3] + 1u;
        for (size_t i = 0; i < sample_count; ++i) {
            if (sample_token >= parser->token_count ||
                parser->tokens[sample_token].parent != values[3] ||
                !canvas_json_decode_sample(
                    parser,
                    sample_token,
                    &operation->samples[i])) {
                result = TG_ERR_INVALID;
                break;
            }
            sample_token = canvas_json_token_after(parser, sample_token);
        }
        if (tg_result_err(result)) {
            break;
        }

        operation_token = values[2];
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

TgResult canvas_document_load_json(
    const char *json,
    CanvasDocument *out_document)
{
    if (out_document == NULL) {
        return TG_ERR_INVALID;
    }
    memset(out_document, 0, sizeof(*out_document));

    CanvasJsonParser parser;
    TgResult result = canvas_json_parse(&parser, json);
    if (tg_result_err(result)) {
        free(parser.tokens);
        return result;
    }

    static const char *const document_names[] = {
        "output_size", "history", "revision",
    };
    static const char *const size_names[] = {"w", "h"};
    static const char *const history_names[] = {
        "head", "tail", "cursor", "operation_count",
    };
    size_t document_values[ARRAY_COUNT(document_names)];
    size_t size_values[ARRAY_COUNT(size_names)];
    size_t history_values[ARRAY_COUNT(history_names)];
    CanvasDocument document;
    memset(&document, 0, sizeof(document));

    if (!canvas_json_object_fields(
            &parser,
            0,
            document_names,
            ARRAY_COUNT(document_names),
            document_values) ||
        !canvas_json_object_fields(
            &parser,
            document_values[0],
            size_names,
            ARRAY_COUNT(size_names),
            size_values) ||
        !canvas_json_object_fields(
            &parser,
            document_values[1],
            history_names,
            ARRAY_COUNT(history_names),
            history_values) ||
        !canvas_json_i32(
            &parser,
            size_values[0],
            &document.output_size.w) ||
        !canvas_json_i32(
            &parser,
            size_values[1],
            &document.output_size.h) ||
        document.output_size.w <= 0 ||
        document.output_size.h <= 0 ||
        !canvas_json_u64(
            &parser,
            document_values[2],
            &document.revision) ||
        document.revision > CANVAS_DOCUMENT_MAX_REVISION) {
        result = TG_ERR_INVALID;
    }

    size_t operation_count = 0;
    size_t tail_index = SIZE_MAX;
    size_t cursor_index = SIZE_MAX;
    bool tail_is_null = false;
    bool cursor_is_null = false;
    if (tg_result_ok(result) &&
        (!canvas_json_size(
             &parser,
             history_values[3],
             &operation_count) ||
         !canvas_json_nullable_index(
             &parser,
             history_values[1],
             &tail_index,
             &tail_is_null) ||
         !canvas_json_nullable_index(
             &parser,
             history_values[2],
             &cursor_index,
             &cursor_is_null))) {
        result = TG_ERR_INVALID;
    }
    if (tg_result_ok(result)) {
        result = canvas_json_decode_operations(
            &parser,
            history_values[0],
            operation_count,
            tail_index,
            tail_is_null,
            cursor_index,
            cursor_is_null,
            &document.history);
    }

    free(parser.tokens);
    if (tg_result_err(result)) {
        canvas_document_destroy(&document);
        return result;
    }
    *out_document = document;
    return TG_OK;
}

TgResult canvas_document_save_json_file(
    const CanvasDocument *document,
    const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return TG_ERR_INVALID;
    }

    char *json = NULL;
    TgResult result = canvas_document_dump_json(document, &json);
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

TgResult canvas_document_load_json_file(
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
        result = canvas_document_load_json(json, out_document);
    }
    free(json);
    return result;
}
