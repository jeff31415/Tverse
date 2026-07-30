#include "canvas_test_hooks.h"
#include "unity.h"

#include <stddef.h>
#include <string.h>

typedef struct ReplayCount {
    size_t count;
} ReplayCount;

static void count_sample(
    const CanvasSample *sample,
    void *userdata)
{
    ReplayCount *count = userdata;
    (void)sample;
    ++count->count;
}

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

static void test_cycle_is_rejected_without_traversal(void)
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

    ReplayCount count = {0};
    canvas_document_replay(&document, count_sample, &count);
    TEST_ASSERT_EQUAL_size_t(0, count.count);
    TEST_ASSERT_FALSE(canvas_document_can_undo(&document));
    TEST_ASSERT_FALSE(canvas_document_can_redo(&document));

    CanvasSample rejected = sample_for('C');
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_document_commit_draw(&document, &rejected, 1));
    TEST_ASSERT_EQUAL_size_t(2, document.history.operation_count);

    TEST_ASSERT_TRUE(canvas_test_history_break_cycle(&document));
    canvas_document_destroy(&document);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cycle_is_rejected_without_traversal);
    return UNITY_END();
}
