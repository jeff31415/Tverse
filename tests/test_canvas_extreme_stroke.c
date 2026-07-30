#include "canvas_state.h"
#include "canvas_test_allocator.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

void setUp(void)
{
    canvas_test_allocator_reset();
}

void tearDown(void)
{
    canvas_test_allocator_reset();
}

static void test_extreme_segment_stops_when_reserve_fails(void)
{
    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){3, 3}));

    TuiCell cell;
    memset(&cell, 0, sizeof(cell));
    cell.ch[0] = 'X';
    cell.width = 1;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state,
            (TgVec2i){INT32_MIN, INT32_MIN},
            cell));

    CanvasSample first = state.pending_stroke.samples[0];
    canvas_test_allocator_fail_after(0);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_NOMEM,
        canvas_state_append_stroke(
            &state,
            (TgVec2i){INT32_MAX, INT32_MAX}));

    TEST_ASSERT_TRUE(canvas_state_stroke_active(&state));
    TEST_ASSERT_EQUAL_size_t(1, state.pending_stroke.sample_count);
    TEST_ASSERT_EQUAL_MEMORY(
        &first,
        &state.pending_stroke.samples[0],
        sizeof(first));
    TEST_ASSERT_EQUAL_size_t(0, state.document.history.operation_count);

    canvas_test_allocator_reset();
    canvas_state_destroy(&state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_extreme_segment_stops_when_reserve_fails);
    return UNITY_END();
}
