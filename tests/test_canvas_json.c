#include "canvas_internal.h"
#include "canvas_json.h"
#include "canvas_json_cjson.h"
#include "canvas_json_jansson.h"
#include "canvas_test_hooks.h"
#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

static CanvasSample full_sample(int32_t x, int32_t y, unsigned seed)
{
    CanvasSample sample;
    memset(&sample, 0, sizeof(sample));
    sample.position = (TgVec2i){x, y};
    for (size_t i = 0; i < sizeof(sample.cell.ch); ++i) {
        sample.cell.ch[i] = (char)(unsigned char)(seed + (unsigned)i * 29u);
    }
    sample.cell.width = (uint8_t)(seed % UINT8_MAX);
    sample.cell.fg = 0x01020304u + seed;
    sample.cell.bg = 0xf0e0d0c0u - seed;
    sample.cell.style = (uint16_t)(0x1200u + seed);
    return sample;
}

static void assert_documents_equal(
    const CanvasDocument *expected,
    const CanvasDocument *actual)
{
    TEST_ASSERT_EQUAL_INT32(
        expected->output_size.w,
        actual->output_size.w);
    TEST_ASSERT_EQUAL_INT32(
        expected->output_size.h,
        actual->output_size.h);
    TEST_ASSERT_EQUAL_UINT64(expected->revision, actual->revision);
    TEST_ASSERT_EQUAL_size_t(
        expected->history.operation_count,
        actual->history.operation_count);

    const CanvasOperation *expected_operation = expected->history.head;
    const CanvasOperation *actual_operation = actual->history.head;
    const CanvasOperation *expected_previous = NULL;
    const CanvasOperation *actual_previous = NULL;
    while (expected_operation != NULL && actual_operation != NULL) {
        TEST_ASSERT_NOT_EQUAL(expected_operation, actual_operation);
        TEST_ASSERT_EQUAL_PTR(expected_previous, expected_operation->prev);
        TEST_ASSERT_EQUAL_PTR(actual_previous, actual_operation->prev);
        TEST_ASSERT_EQUAL_INT(
            expected_operation->type,
            actual_operation->type);
        TEST_ASSERT_EQUAL_size_t(
            expected_operation->sample_count,
            actual_operation->sample_count);
        TEST_ASSERT_NOT_EQUAL(
            expected_operation->samples,
            actual_operation->samples);
        TEST_ASSERT_EQUAL_MEMORY(
            expected_operation->samples,
            actual_operation->samples,
            expected_operation->sample_count *
                sizeof(*expected_operation->samples));
        TEST_ASSERT_EQUAL_INT(
            expected_operation == expected->history.cursor,
            actual_operation == actual->history.cursor);

        expected_previous = expected_operation;
        actual_previous = actual_operation;
        expected_operation = expected_operation->next;
        actual_operation = actual_operation->next;
    }
    TEST_ASSERT_NULL(expected_operation);
    TEST_ASSERT_NULL(actual_operation);
    TEST_ASSERT_EQUAL_PTR(expected_previous, expected->history.tail);
    TEST_ASSERT_EQUAL_PTR(actual_previous, actual->history.tail);
}

static void make_document_with_redo(CanvasDocument *document)
{
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_init(document, (TgSizei){37, 19}));

    CanvasSample first[] = {
        full_sample(INT32_MIN, INT32_MAX, 7u),
        full_sample(-15, 22, 31u),
    };
    CanvasSample second[] = {
        full_sample(0, 0, 83u),
    };
    CanvasSample third[] = {
        full_sample(44, -91, 129u),
        full_sample(2, 3, 211u),
        full_sample(INT32_MAX, INT32_MIN, 253u),
    };
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_commit_draw(
            document,
            first,
            sizeof(first) / sizeof(first[0])));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_commit_draw(
            document,
            second,
            sizeof(second) / sizeof(second[0])));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_commit_draw(
            document,
            third,
            sizeof(third) / sizeof(third[0])));
    TEST_ASSERT_TRUE(canvas_document_undo(document));
    TEST_ASSERT_TRUE(canvas_document_can_redo(document));
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_dump_load_roundtrip_preserves_every_field(void)
{
    CanvasDocument original;
    make_document_with_redo(&original);

    char *json = NULL;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_dump_json(&original, &json));
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"head\":{\"type\":"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"next\":{\"type\":"));
    TEST_ASSERT_NULL(strstr(json, "\"operations\":"));

    CanvasDocument loaded;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_load_json(json, &loaded));
    assert_documents_equal(&original, &loaded);

    char *loaded_json = NULL;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_dump_json(&loaded, &loaded_json));
    TEST_ASSERT_EQUAL_STRING(json, loaded_json);

    free(loaded_json);
    free(json);
    canvas_document_destroy(&loaded);
    canvas_document_destroy(&original);
}

typedef TgResult (*CanvasDumpJsonFn)(
    const CanvasDocument *document,
    char **out_json);
typedef TgResult (*CanvasLoadJsonFn)(
    const char *json,
    CanvasDocument *out_document);

static void test_all_backends_match_and_cross_load(void)
{
    CanvasDocument original;
    make_document_with_redo(&original);

    CanvasDumpJsonFn dump_functions[] = {
        canvas_document_dump_json,
        canvas_document_dump_json_cjson,
        canvas_document_dump_json_jansson,
    };
    CanvasLoadJsonFn load_functions[] = {
        canvas_document_load_json,
        canvas_document_load_json_cjson,
        canvas_document_load_json_jansson,
    };
    char *json[ARRAY_COUNT(dump_functions)];
    memset(json, 0, sizeof(json));
    for (size_t i = 0; i < ARRAY_COUNT(dump_functions); ++i) {
        TEST_ASSERT_EQUAL_INT(
            TG_OK,
            dump_functions[i](&original, &json[i]));
        TEST_ASSERT_NOT_NULL(json[i]);
        TEST_ASSERT_EQUAL_STRING(json[0], json[i]);
    }

    for (size_t dump_index = 0;
         dump_index < ARRAY_COUNT(dump_functions);
         ++dump_index) {
        for (size_t load_index = 0;
             load_index < ARRAY_COUNT(load_functions);
             ++load_index) {
            CanvasDocument loaded;
            TEST_ASSERT_EQUAL_INT(
                TG_OK,
                load_functions[load_index](json[dump_index], &loaded));
            assert_documents_equal(&original, &loaded);
            canvas_document_destroy(&loaded);
        }
    }

    for (size_t i = 0; i < ARRAY_COUNT(json); ++i) {
        free(json[i]);
    }
    canvas_document_destroy(&original);
}

static void test_revision_limit_is_global(void)
{
    CanvasDocument original;
    make_document_with_redo(&original);
    original.revision = CANVAS_DOCUMENT_MAX_REVISION;

    char *custom_json = NULL;
    char *cjson_json = NULL;
    char *jansson_json = NULL;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_dump_json(&original, &custom_json));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_dump_json_cjson(&original, &cjson_json));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_dump_json_jansson(&original, &jansson_json));
    TEST_ASSERT_EQUAL_STRING(custom_json, cjson_json);
    TEST_ASSERT_EQUAL_STRING(custom_json, jansson_json);
    TEST_ASSERT_NOT_NULL(strstr(cjson_json, "9223372036854775807"));

    CanvasDocument custom_loaded;
    CanvasDocument jansson_loaded;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_load_json(cjson_json, &custom_loaded));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_load_json_jansson(cjson_json, &jansson_loaded));
    assert_documents_equal(&original, &custom_loaded);
    assert_documents_equal(&original, &jansson_loaded);

    CanvasDocument cjson_rejected;
    memset(&cjson_rejected, 0xA5, sizeof(cjson_rejected));
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_UNSUPPORTED,
        canvas_document_load_json_cjson(
            custom_json,
            &cjson_rejected));
    TEST_ASSERT_NULL(cjson_rejected.history.head);
    TEST_ASSERT_EQUAL_INT32(0, cjson_rejected.output_size.w);

    char *over_limit = strstr(jansson_json, "9223372036854775807");
    TEST_ASSERT_NOT_NULL(over_limit);
    over_limit[strlen("922337203685477580")] = '8';
    CanvasDocument over_limit_rejected;
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json(jansson_json, &over_limit_rejected));
    TEST_ASSERT_NULL(over_limit_rejected.history.head);
    TEST_ASSERT_NOT_EQUAL(
        TG_OK,
        canvas_document_load_json_cjson(
            jansson_json,
            &over_limit_rejected));
    TEST_ASSERT_NULL(over_limit_rejected.history.head);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json_jansson(
            jansson_json,
            &over_limit_rejected));
    TEST_ASSERT_NULL(over_limit_rejected.history.head);

    TEST_ASSERT_TRUE(canvas_document_redo(&original));
    TEST_ASSERT_EQUAL_UINT64(
        CANVAS_DOCUMENT_MAX_REVISION,
        original.revision);
    TEST_ASSERT_TRUE(canvas_document_undo(&original));
    TEST_ASSERT_EQUAL_UINT64(
        CANVAS_DOCUMENT_MAX_REVISION,
        original.revision);

    original.revision = CANVAS_DOCUMENT_MAX_REVISION + UINT64_C(1);
    free(jansson_json);
    free(cjson_json);
    free(custom_json);
    custom_json = (char *)(uintptr_t)1u;
    cjson_json = (char *)(uintptr_t)1u;
    jansson_json = (char *)(uintptr_t)1u;
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_dump_json(&original, &custom_json));
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_dump_json_cjson(&original, &cjson_json));
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_dump_json_jansson(&original, &jansson_json));
    TEST_ASSERT_NULL(custom_json);
    TEST_ASSERT_NULL(cjson_json);
    TEST_ASSERT_NULL(jansson_json);
    TEST_ASSERT_FALSE(canvas_document_can_undo(&original));
    TEST_ASSERT_FALSE(canvas_document_can_redo(&original));
    canvas_document_reset(&original);
    TEST_ASSERT_EQUAL_UINT64(
        CANVAS_DOCUMENT_MAX_REVISION,
        original.revision);
    TEST_ASSERT_EQUAL_size_t(0, original.history.operation_count);

    canvas_document_destroy(&jansson_loaded);
    canvas_document_destroy(&custom_loaded);
    canvas_document_destroy(&original);
}

static void test_empty_and_fully_undone_documents_roundtrip(void)
{
    CanvasDocument empty;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_init(&empty, (TgSizei){1, 1}));
    char *json = NULL;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_dump_json(&empty, &json));
    CanvasDocument empty_loaded;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_load_json(json, &empty_loaded));
    assert_documents_equal(&empty, &empty_loaded);
    free(json);
    canvas_document_destroy(&empty_loaded);
    canvas_document_destroy(&empty);

    CanvasDocument undone;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_init(&undone, (TgSizei){5, 3}));
    CanvasSample first = full_sample(1, 2, 11u);
    CanvasSample second = full_sample(3, 4, 17u);
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_commit_draw(&undone, &first, 1));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_commit_draw(&undone, &second, 1));
    TEST_ASSERT_TRUE(canvas_document_undo(&undone));
    TEST_ASSERT_TRUE(canvas_document_undo(&undone));
    TEST_ASSERT_NULL(undone.history.cursor);

    json = NULL;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_dump_json(&undone, &json));
    CanvasDocument undone_loaded;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_load_json(json, &undone_loaded));
    assert_documents_equal(&undone, &undone_loaded);
    TEST_ASSERT_TRUE(canvas_document_can_redo(&undone_loaded));
    free(json);
    canvas_document_destroy(&undone_loaded);
    canvas_document_destroy(&undone);
}

static void test_dump_rejects_cycle_and_load_rejects_bad_links(void)
{
    CanvasDocument document;
    make_document_with_redo(&document);
    TEST_ASSERT_TRUE(canvas_test_history_make_cycle(&document));

    char *json = (char *)(uintptr_t)1u;
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_dump_json(&document, &json));
    TEST_ASSERT_NULL(json);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_dump_json_cjson(&document, &json));
    TEST_ASSERT_NULL(json);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_dump_json_jansson(&document, &json));
    TEST_ASSERT_NULL(json);
    TEST_ASSERT_TRUE(canvas_test_history_break_cycle(&document));

    CanvasOperation *second = document.history.head->next;
    TEST_ASSERT_NOT_NULL(second);
    CanvasOperation *expected_previous = second->prev;
    second->prev = NULL;
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_dump_json(&document, &json));
    TEST_ASSERT_NULL(json);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_dump_json_cjson(&document, &json));
    TEST_ASSERT_NULL(json);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_dump_json_jansson(&document, &json));
    TEST_ASSERT_NULL(json);
    second->prev = expected_previous;

    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_dump_json(&document, &json));

    char *first_previous = strstr(json, "\"prev\":null");
    TEST_ASSERT_NOT_NULL(first_previous);
    char *second_previous = strstr(first_previous + 1, "\"prev\":0");
    TEST_ASSERT_NOT_NULL(second_previous);
    second_previous[strlen("\"prev\":")] = '1';
    CanvasDocument rejected;
    memset(&rejected, 0xA5, sizeof(rejected));
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json(json, &rejected));
    TEST_ASSERT_NULL(rejected.history.head);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json_cjson(json, &rejected));
    TEST_ASSERT_NULL(rejected.history.head);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json_jansson(json, &rejected));
    TEST_ASSERT_NULL(rejected.history.head);
    second_previous[strlen("\"prev\":")] = '0';

    char *count = strstr(json, "\"operation_count\":3");
    TEST_ASSERT_NOT_NULL(count);
    count[strlen("\"operation_count\":")] = '4';

    memset(&rejected, 0xA5, sizeof(rejected));
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json(json, &rejected));
    TEST_ASSERT_NULL(rejected.history.head);
    TEST_ASSERT_EQUAL_INT32(0, rejected.output_size.w);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json_cjson(json, &rejected));
    TEST_ASSERT_NULL(rejected.history.head);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json_jansson(json, &rejected));
    TEST_ASSERT_NULL(rejected.history.head);
    free(json);

    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json("{}", &rejected));
    TEST_ASSERT_NULL(rejected.history.head);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json_jansson("{}", &rejected));
    TEST_ASSERT_NULL(rejected.history.head);
    canvas_document_destroy(&document);
}

static void test_jansson_rejects_duplicate_and_unknown_fields(void)
{
    CanvasDocument rejected;
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json_jansson(
            "{\"output_size\":{},\"output_size\":{},"
            "\"history\":{},\"revision\":0}",
            &rejected));
    TEST_ASSERT_NULL(rejected.history.head);

    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_load_json_jansson(
            "{\"output_size\":{\"w\":1,\"h\":1},"
            "\"history\":{\"head\":null,\"tail\":null,"
            "\"cursor\":null,\"operation_count\":0},"
            "\"revision\":0,\"unknown\":false}",
            &rejected));
    TEST_ASSERT_NULL(rejected.history.head);
}

static void test_json_file_save_and_load_roundtrip(void)
{
    static const char path[] = "canvas_json_roundtrip_test.json";
    CanvasDocument original;
    make_document_with_redo(&original);
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_save_json_file(&original, path));

    CanvasDocument loaded;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_load_json_file(path, &loaded));
    assert_documents_equal(&original, &loaded);
    TEST_ASSERT_EQUAL_INT(0, remove(path));

    canvas_document_destroy(&loaded);
    canvas_document_destroy(&original);
}

static void test_cjson_file_save_and_cross_load_roundtrip(void)
{
    static const char path[] = "canvas_cjson_roundtrip_test.json";
    CanvasDocument original;
    make_document_with_redo(&original);
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_save_json_file_cjson(&original, path));

    CanvasDocument cjson_loaded;
    CanvasDocument custom_loaded;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_load_json_file_cjson(path, &cjson_loaded));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_load_json_file(path, &custom_loaded));
    assert_documents_equal(&original, &cjson_loaded);
    assert_documents_equal(&original, &custom_loaded);
    TEST_ASSERT_EQUAL_INT(0, remove(path));

    canvas_document_destroy(&custom_loaded);
    canvas_document_destroy(&cjson_loaded);
    canvas_document_destroy(&original);
}

static void test_jansson_file_save_and_cross_load_roundtrip(void)
{
    static const char path[] = "canvas_jansson_roundtrip_test.json";
    CanvasDocument original;
    make_document_with_redo(&original);
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_save_json_file_jansson(&original, path));

    CanvasDocument jansson_loaded;
    CanvasDocument custom_loaded;
    CanvasDocument cjson_loaded;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_load_json_file_jansson(path, &jansson_loaded));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_load_json_file(path, &custom_loaded));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_load_json_file_cjson(path, &cjson_loaded));
    assert_documents_equal(&original, &jansson_loaded);
    assert_documents_equal(&original, &custom_loaded);
    assert_documents_equal(&original, &cjson_loaded);
    TEST_ASSERT_EQUAL_INT(0, remove(path));

    canvas_document_destroy(&cjson_loaded);
    canvas_document_destroy(&custom_loaded);
    canvas_document_destroy(&jansson_loaded);
    canvas_document_destroy(&original);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dump_load_roundtrip_preserves_every_field);
    RUN_TEST(test_all_backends_match_and_cross_load);
    RUN_TEST(test_revision_limit_is_global);
    RUN_TEST(test_empty_and_fully_undone_documents_roundtrip);
    RUN_TEST(test_dump_rejects_cycle_and_load_rejects_bad_links);
    RUN_TEST(test_jansson_rejects_duplicate_and_unknown_fields);
    RUN_TEST(test_json_file_save_and_load_roundtrip);
    RUN_TEST(test_cjson_file_save_and_cross_load_roundtrip);
    RUN_TEST(test_jansson_file_save_and_cross_load_roundtrip);
    return UNITY_END();
}
