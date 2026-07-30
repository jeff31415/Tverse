#include "canvas_test_hooks.h"
#include "unity.h"

#include <string.h>

static CanvasSample sample_for(char ch)
{
    CanvasSample sample;
    memset(&sample, 0, sizeof(sample));
    sample.cell.ch[0] = ch;
    sample.cell.width = 1;
    return sample;
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_destroy_terminates_with_cycle(void)
{
    CanvasDocument document;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_init(&document, (TgSizei){3, 3}));
    CanvasSample first = sample_for('A');
    CanvasSample second = sample_for('B');
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_commit_draw(&document, &first, 1));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_commit_draw(&document, &second, 1));
    TEST_ASSERT_TRUE(canvas_test_history_make_cycle(&document));

    canvas_document_destroy(&document);
    TEST_ASSERT_NULL(document.history.head);
    TEST_ASSERT_NULL(document.history.tail);
    TEST_ASSERT_NULL(document.history.cursor);
    TEST_ASSERT_EQUAL_size_t(0, document.history.operation_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_destroy_terminates_with_cycle);
    return UNITY_END();
}
