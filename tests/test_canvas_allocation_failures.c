#include "canvas_state.h"
#include "canvas_test_allocator.h"
#include "unity.h"

#include <string.h>

static TuiCell cell_for(char ch)
{
    TuiCell cell;
    memset(&cell, 0, sizeof(cell));
    cell.ch[0] = ch;
    cell.width = 1;
    return cell;
}

void setUp(void)
{
    canvas_test_allocator_reset();
}

void tearDown(void)
{
    canvas_test_allocator_reset();
}

static void test_begin_failure_returns_to_idle(void)
{
    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){3, 3}));

    canvas_test_allocator_fail_after(0);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_NOMEM,
        canvas_state_begin_stroke(
            &state, (TgVec2i){0, 0}, cell_for('A')));
    TEST_ASSERT_FALSE(canvas_state_stroke_active(&state));
    TEST_ASSERT_EQUAL_size_t(0, state.pending_stroke.sample_count);
    TEST_ASSERT_EQUAL_size_t(0, state.pending_stroke.sample_capacity);
    TEST_ASSERT_NULL(state.pending_stroke.samples);
    TEST_ASSERT_EQUAL_size_t(0, state.document.history.operation_count);

    canvas_test_allocator_reset();
    canvas_state_destroy(&state);
}

static void test_append_growth_failure_preserves_samples(void)
{
    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){3, 3}));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state, (TgVec2i){0, 0}, cell_for('A')));

    CanvasSample original = state.pending_stroke.samples[0];
    CanvasSample *storage = state.pending_stroke.samples;
    size_t capacity = state.pending_stroke.sample_capacity;
    canvas_test_allocator_fail_after(0);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_NOMEM,
        canvas_state_append_stroke(&state, (TgVec2i){32, 0}));

    TEST_ASSERT_TRUE(canvas_state_stroke_active(&state));
    TEST_ASSERT_EQUAL_size_t(1, state.pending_stroke.sample_count);
    TEST_ASSERT_EQUAL_size_t(capacity, state.pending_stroke.sample_capacity);
    TEST_ASSERT_EQUAL_PTR(storage, state.pending_stroke.samples);
    TEST_ASSERT_EQUAL_MEMORY(
        &original,
        &state.pending_stroke.samples[0],
        sizeof(original));
    TEST_ASSERT_EQUAL_size_t(0, state.document.history.operation_count);

    canvas_test_allocator_reset();
    canvas_state_destroy(&state);
}

static void test_finalize_operation_allocation_failure_is_atomic(void)
{
    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){3, 3}));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state, (TgVec2i){1, 2}, cell_for('A')));

    CanvasSample original = state.pending_stroke.samples[0];
    uint64_t revision = state.document.revision;
    canvas_test_allocator_fail_after(0);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_NOMEM,
        canvas_state_finalize_stroke(&state));

    TEST_ASSERT_TRUE(canvas_state_stroke_active(&state));
    TEST_ASSERT_EQUAL_size_t(1, state.pending_stroke.sample_count);
    TEST_ASSERT_EQUAL_MEMORY(
        &original,
        &state.pending_stroke.samples[0],
        sizeof(original));
    TEST_ASSERT_EQUAL_size_t(0, state.document.history.operation_count);
    TEST_ASSERT_EQUAL_UINT64(revision, state.document.revision);

    canvas_test_allocator_reset();
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_finalize_stroke(&state));
    canvas_state_destroy(&state);
}

static void test_finalize_sample_allocation_failure_is_atomic(void)
{
    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){3, 3}));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state, (TgVec2i){1, 2}, cell_for('A')));

    CanvasSample original = state.pending_stroke.samples[0];
    uint64_t revision = state.document.revision;
    canvas_test_allocator_fail_after(1);
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_NOMEM,
        canvas_state_finalize_stroke(&state));

    TEST_ASSERT_TRUE(canvas_state_stroke_active(&state));
    TEST_ASSERT_EQUAL_size_t(1, state.pending_stroke.sample_count);
    TEST_ASSERT_EQUAL_MEMORY(
        &original,
        &state.pending_stroke.samples[0],
        sizeof(original));
    TEST_ASSERT_EQUAL_size_t(0, state.document.history.operation_count);
    TEST_ASSERT_EQUAL_UINT64(revision, state.document.revision);

    canvas_test_allocator_reset();
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_finalize_stroke(&state));
    canvas_state_destroy(&state);
}

static void test_redo_branch_survives_pending_finalize_failure(void)
{
    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){3, 3}));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state, (TgVec2i){0, 0}, cell_for('A')));
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_finalize_stroke(&state));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state, (TgVec2i){1, 0}, cell_for('B')));
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_finalize_stroke(&state));
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_undo(&state));
    TEST_ASSERT_TRUE(canvas_state_can_redo(&state));

    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state, (TgVec2i){-1, 0}, cell_for('C')));
    CanvasOperation *redo_tail = state.document.history.tail;
    CanvasOperation *cursor = state.document.history.cursor;
    uint64_t revision = state.document.revision;

    canvas_test_allocator_fail_after(0);
    TEST_ASSERT_EQUAL_INT(TG_ERR_NOMEM, canvas_state_redo(&state));
    TEST_ASSERT_TRUE(canvas_state_stroke_active(&state));
    TEST_ASSERT_TRUE(canvas_state_can_redo(&state));
    TEST_ASSERT_EQUAL_PTR(redo_tail, state.document.history.tail);
    TEST_ASSERT_EQUAL_PTR(cursor, state.document.history.cursor);
    TEST_ASSERT_EQUAL_size_t(2, state.document.history.operation_count);
    TEST_ASSERT_EQUAL_UINT64(revision, state.document.revision);

    canvas_test_allocator_reset();
    canvas_state_cancel_stroke(&state);
    canvas_state_destroy(&state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_failure_returns_to_idle);
    RUN_TEST(test_append_growth_failure_preserves_samples);
    RUN_TEST(test_finalize_operation_allocation_failure_is_atomic);
    RUN_TEST(test_finalize_sample_allocation_failure_is_atomic);
    RUN_TEST(test_redo_branch_survives_pending_finalize_failure);
    return UNITY_END();
}
