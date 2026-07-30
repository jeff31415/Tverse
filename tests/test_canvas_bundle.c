#include "canvas_state.h"
#include "unity.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

typedef struct ReplayCapture {
    CanvasSample samples[128];
    size_t count;
    bool overflow;
} ReplayCapture;

static void capture_sample(
    const CanvasSample *sample,
    void *userdata)
{
    ReplayCapture *capture = userdata;
    if (capture->count >= ARRAY_COUNT(capture->samples)) {
        capture->overflow = true;
        return;
    }
    capture->samples[capture->count++] = *sample;
}

static ReplayCapture replay(const CanvasDocument *document)
{
    ReplayCapture capture;
    memset(&capture, 0, sizeof(capture));
    canvas_document_replay(document, capture_sample, &capture);
    return capture;
}

static CanvasSample sample_at(int32_t x, int32_t y, unsigned char ch)
{
    CanvasSample sample;
    memset(&sample, 0, sizeof(sample));
    sample.position = (TgVec2i){x, y};
    sample.cell.ch[0] = (char)ch;
    sample.cell.width = 1;
    sample.cell.fg = TUI_COLOR_DEFAULT;
    sample.cell.bg = TUI_COLOR_DEFAULT;
    return sample;
}

static TuiCell cell_for(unsigned char ch)
{
    TuiCell cell;
    memset(&cell, 0, sizeof(cell));
    cell.ch[0] = (char)ch;
    cell.width = 1;
    cell.fg = TUI_COLOR_DEFAULT;
    cell.bg = TUI_COLOR_DEFAULT;
    cell.style = TUI_STYLE_BOLD;
    return cell;
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_output_bounds_and_init_validation(void)
{
    CanvasDocument odd;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_init(&odd, (TgSizei){5, 3}));
    TEST_ASSERT_TRUE(
        canvas_document_contains_output(&odd, (TgVec2i){-2, -1}));
    TEST_ASSERT_TRUE(
        canvas_document_contains_output(&odd, (TgVec2i){2, 1}));
    TEST_ASSERT_FALSE(
        canvas_document_contains_output(&odd, (TgVec2i){-3, 0}));
    TEST_ASSERT_FALSE(
        canvas_document_contains_output(&odd, (TgVec2i){3, 0}));
    canvas_document_destroy(&odd);

    CanvasDocument even;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_init(&even, (TgSizei){4, 2}));
    TEST_ASSERT_TRUE(
        canvas_document_contains_output(&even, (TgVec2i){-2, -1}));
    TEST_ASSERT_TRUE(
        canvas_document_contains_output(&even, (TgVec2i){1, 0}));
    TEST_ASSERT_FALSE(
        canvas_document_contains_output(&even, (TgVec2i){2, 0}));
    TEST_ASSERT_FALSE(
        canvas_document_contains_output(&even, (TgVec2i){0, 1}));
    canvas_document_destroy(&even);

    CanvasState state;
    memset(&state, 0xA5, sizeof(state));
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_state_init(&state, (TgSizei){0, 1}));
    TEST_ASSERT_EQUAL_INT32(0, state.document.output_size.w);
    TEST_ASSERT_NULL(state.pending_stroke.samples);
    canvas_state_destroy(&state);

    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_state_init(NULL, (TgSizei){1, 1}));
}

static void test_history_branching_and_reset(void)
{
    CanvasDocument document;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_init(&document, (TgSizei){4, 2}));

    CanvasSample first = sample_at(0, 0, 'A');
    CanvasSample draft = sample_at(20, 10, 'B');
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_commit_draw(&document, &first, 1));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_commit_draw(&document, &draft, 1));
    TEST_ASSERT_EQUAL_size_t(2, document.history.operation_count);

    ReplayCapture capture = replay(&document);
    TEST_ASSERT_FALSE(capture.overflow);
    TEST_ASSERT_EQUAL_size_t(2, capture.count);
    TEST_ASSERT_EQUAL_CHAR('A', capture.samples[0].cell.ch[0]);
    TEST_ASSERT_EQUAL_INT32(20, capture.samples[1].position.x);

    TEST_ASSERT_TRUE(canvas_document_undo(&document));
    capture = replay(&document);
    TEST_ASSERT_EQUAL_size_t(1, capture.count);
    TEST_ASSERT_TRUE(canvas_document_can_redo(&document));

    TEST_ASSERT_TRUE(canvas_document_redo(&document));
    TEST_ASSERT_EQUAL_size_t(2, replay(&document).count);

    TEST_ASSERT_TRUE(canvas_document_undo(&document));
    CanvasSample branch = sample_at(1, 0, 'C');
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_document_commit_draw(&document, &branch, 1));
    TEST_ASSERT_FALSE(canvas_document_can_redo(&document));
    TEST_ASSERT_EQUAL_size_t(2, document.history.operation_count);

    capture = replay(&document);
    TEST_ASSERT_EQUAL_size_t(2, capture.count);
    TEST_ASSERT_EQUAL_CHAR('A', capture.samples[0].cell.ch[0]);
    TEST_ASSERT_EQUAL_CHAR('C', capture.samples[1].cell.ch[0]);

    uint64_t revision = document.revision;
    canvas_document_reset(&document);
    TEST_ASSERT_GREATER_THAN_UINT64(revision, document.revision);
    TEST_ASSERT_FALSE(canvas_document_can_undo(&document));
    TEST_ASSERT_FALSE(canvas_document_can_redo(&document));
    TEST_ASSERT_EQUAL_size_t(0, replay(&document).count);
    canvas_document_destroy(&document);
}

static void test_viewport_projection_boundaries(void)
{
    CanvasViewport viewport = {
        .rect = {2, 1, 7, 5},
        .origin_screen = {5, 3},
    };
    TgVec2i position = {77, 88};
    TEST_ASSERT_TRUE(canvas_viewport_screen_to_world(
        &viewport, 5, 3, &position));
    TEST_ASSERT_EQUAL_INT32(0, position.x);
    TEST_ASSERT_EQUAL_INT32(0, position.y);
    TEST_ASSERT_TRUE(canvas_viewport_screen_to_world(
        &viewport, 2, 1, &position));
    TEST_ASSERT_EQUAL_INT32(-3, position.x);
    TEST_ASSERT_EQUAL_INT32(-2, position.y);

    position = (TgVec2i){77, 88};
    TEST_ASSERT_FALSE(canvas_viewport_screen_to_world(
        &viewport, 9, 3, &position));
    TEST_ASSERT_EQUAL_INT32(77, position.x);
    TEST_ASSERT_EQUAL_INT32(88, position.y);

    TgVec2i screen = {66, 55};
    TEST_ASSERT_TRUE(canvas_viewport_world_to_screen(
        &viewport, (TgVec2i){-3, -2}, &screen));
    TEST_ASSERT_EQUAL_INT32(2, screen.x);
    TEST_ASSERT_EQUAL_INT32(1, screen.y);

    screen = (TgVec2i){66, 55};
    TEST_ASSERT_FALSE(canvas_viewport_world_to_screen(
        &viewport, (TgVec2i){INT32_MAX, INT32_MAX}, &screen));
    TEST_ASSERT_EQUAL_INT32(66, screen.x);
    TEST_ASSERT_EQUAL_INT32(55, screen.y);

    TEST_ASSERT_FALSE(canvas_viewport_screen_to_world(
        NULL, 0, 0, &position));
    TEST_ASSERT_FALSE(canvas_viewport_screen_to_world(
        &viewport, 0, 0, NULL));
    TEST_ASSERT_FALSE(canvas_viewport_world_to_screen(
        NULL, (TgVec2i){0, 0}, &screen));
    TEST_ASSERT_FALSE(canvas_viewport_world_to_screen(
        &viewport, (TgVec2i){0, 0}, NULL));
}

static void test_idle_and_null_state_contract(void)
{
    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){3, 3}));

    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_state_append_stroke(&state, (TgVec2i){1, 1}));
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_finalize_stroke(&state));
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_undo(&state));
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_redo(&state));
    TEST_ASSERT_FALSE(canvas_state_stroke_active(&state));
    TEST_ASSERT_FALSE(canvas_state_can_undo(&state));
    TEST_ASSERT_FALSE(canvas_state_can_redo(&state));
    TEST_ASSERT_EQUAL_UINT64(0, state.document.revision);

    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_state_begin_stroke(
            NULL, (TgVec2i){0, 0}, cell_for('X')));
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_state_append_stroke(NULL, (TgVec2i){0, 0}));
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_state_finalize_stroke(NULL));
    TEST_ASSERT_EQUAL_INT(TG_ERR_INVALID, canvas_state_undo(NULL));
    TEST_ASSERT_EQUAL_INT(TG_ERR_INVALID, canvas_state_redo(NULL));
    TEST_ASSERT_FALSE(canvas_state_stroke_active(NULL));
    TEST_ASSERT_FALSE(canvas_state_can_undo(NULL));
    TEST_ASSERT_FALSE(canvas_state_can_redo(NULL));
    canvas_state_cancel_stroke(NULL);
    canvas_state_reset(NULL);
    canvas_state_destroy(NULL);

    canvas_state_destroy(&state);
    canvas_state_destroy(&state);
}

static void test_begin_cancel_and_reset_preserve_storage(void)
{
    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){5, 3}));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state, (TgVec2i){0, 0}, cell_for('A')));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_append_stroke(&state, (TgVec2i){3, 0}));

    size_t capacity = state.pending_stroke.sample_capacity;
    CanvasSample *storage = state.pending_stroke.samples;
    TEST_ASSERT_GREATER_THAN_size_t(0, capacity);

    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state, (TgVec2i){9, 9}, cell_for('B')));
    TEST_ASSERT_TRUE(canvas_state_stroke_active(&state));
    TEST_ASSERT_EQUAL_size_t(1, state.pending_stroke.sample_count);
    TEST_ASSERT_EQUAL_size_t(1, state.document.history.operation_count);
    TEST_ASSERT_EQUAL_PTR(storage, state.pending_stroke.samples);

    canvas_state_cancel_stroke(&state);
    TEST_ASSERT_FALSE(canvas_state_stroke_active(&state));
    TEST_ASSERT_EQUAL_size_t(0, state.pending_stroke.sample_count);
    TEST_ASSERT_EQUAL_size_t(capacity, state.pending_stroke.sample_capacity);
    TEST_ASSERT_EQUAL_size_t(1, state.document.history.operation_count);

    canvas_state_reset(&state);
    TEST_ASSERT_EQUAL_INT32(5, state.document.output_size.w);
    TEST_ASSERT_EQUAL_INT32(3, state.document.output_size.h);
    TEST_ASSERT_EQUAL_size_t(0, state.document.history.operation_count);
    TEST_ASSERT_EQUAL_size_t(capacity, state.pending_stroke.sample_capacity);
    TEST_ASSERT_EQUAL_PTR(storage, state.pending_stroke.samples);
    canvas_state_destroy(&state);
}

static void test_undo_redo_finalize_pending_and_branch(void)
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
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_undo(&state));
    TEST_ASSERT_FALSE(canvas_state_stroke_active(&state));
    TEST_ASSERT_TRUE(canvas_state_can_redo(&state));
    ReplayCapture capture = replay(&state.document);
    TEST_ASSERT_EQUAL_size_t(1, capture.count);
    TEST_ASSERT_EQUAL_CHAR('A', capture.samples[0].cell.ch[0]);

    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_redo(&state));
    capture = replay(&state.document);
    TEST_ASSERT_EQUAL_size_t(2, capture.count);
    TEST_ASSERT_EQUAL_CHAR('B', capture.samples[1].cell.ch[0]);

    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_undo(&state));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state, (TgVec2i){-1, 0}, cell_for('C')));
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_redo(&state));
    TEST_ASSERT_FALSE(canvas_state_stroke_active(&state));
    TEST_ASSERT_FALSE(canvas_state_can_redo(&state));

    capture = replay(&state.document);
    TEST_ASSERT_EQUAL_size_t(2, capture.count);
    TEST_ASSERT_EQUAL_CHAR('A', capture.samples[0].cell.ch[0]);
    TEST_ASSERT_EQUAL_CHAR('C', capture.samples[1].cell.ch[0]);
    canvas_state_destroy(&state);
}

static void test_bresenham_all_octants_and_duplicates(void)
{
    static const TgVec2i endpoints[] = {
        {5, 2}, {2, 5}, {-2, 5}, {-5, 2},
        {-5, -2}, {-2, -5}, {2, -5}, {5, -2},
        {5, 0}, {0, 5}, {-5, 0}, {0, -5},
    };

    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){3, 3}));

    for (size_t endpoint_index = 0;
         endpoint_index < ARRAY_COUNT(endpoints);
         ++endpoint_index) {
        TgVec2i endpoint = endpoints[endpoint_index];
        TEST_ASSERT_EQUAL_INT(
            TG_OK,
            canvas_state_begin_stroke(
                &state, (TgVec2i){0, 0}, cell_for('X')));
        TEST_ASSERT_EQUAL_INT(
            TG_OK,
            canvas_state_append_stroke(&state, endpoint));

        int64_t abs_x = endpoint.x < 0
            ? -(int64_t)endpoint.x
            : endpoint.x;
        int64_t abs_y = endpoint.y < 0
            ? -(int64_t)endpoint.y
            : endpoint.y;
        size_t expected_count =
            (size_t)(abs_x > abs_y ? abs_x : abs_y) + 1u;
        TEST_ASSERT_EQUAL_size_t(
            expected_count,
            state.pending_stroke.sample_count);

        CanvasSample *samples = state.pending_stroke.samples;
        TEST_ASSERT_EQUAL_INT32(0, samples[0].position.x);
        TEST_ASSERT_EQUAL_INT32(0, samples[0].position.y);
        TEST_ASSERT_EQUAL_INT32(
            endpoint.x,
            samples[expected_count - 1u].position.x);
        TEST_ASSERT_EQUAL_INT32(
            endpoint.y,
            samples[expected_count - 1u].position.y);

        for (size_t sample_index = 1;
             sample_index < expected_count;
             ++sample_index) {
            int64_t dx =
                (int64_t)samples[sample_index].position.x -
                samples[sample_index - 1u].position.x;
            int64_t dy =
                (int64_t)samples[sample_index].position.y -
                samples[sample_index - 1u].position.y;
            TEST_ASSERT_TRUE(dx >= -1 && dx <= 1);
            TEST_ASSERT_TRUE(dy >= -1 && dy <= 1);
            TEST_ASSERT_TRUE(dx != 0 || dy != 0);
        }

        TEST_ASSERT_EQUAL_INT(
            TG_OK,
            canvas_state_append_stroke(&state, endpoint));
        TEST_ASSERT_EQUAL_size_t(
            expected_count,
            state.pending_stroke.sample_count);
        canvas_state_cancel_stroke(&state);
    }

    canvas_state_destroy(&state);
}

static void test_state_stroke_and_render(void)
{
    enum {
        FRAME_WIDTH = 7,
        FRAME_HEIGHT = 5
    };
    const uint32_t foreground = 0x010203u;
    const uint32_t draft_background = 0x111213u;
    const uint32_t output_background = 0x212223u;
    TuiCell cells[FRAME_WIDTH * FRAME_HEIGHT];
    memset(cells, 0, sizeof(cells));

    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){3, 3}));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state, (TgVec2i){0, 0}, cell_for('X')));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_append_stroke(&state, (TgVec2i){2, 0}));

    CanvasRenderTarget target = {
        .size = {FRAME_WIDTH, FRAME_HEIGHT},
        .cells = cells,
        .viewport = {
            .rect = {0, 0, FRAME_WIDTH, FRAME_HEIGHT},
            .origin_screen = {3, 2},
        },
        .style = {
            .default_fg = foreground,
            .draft_bg = draft_background,
            .output_bg = output_background,
        },
    };
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_render(&state, &target));

    size_t origin =
        (size_t)target.viewport.origin_screen.y * FRAME_WIDTH +
        (size_t)target.viewport.origin_screen.x;
    TEST_ASSERT_EQUAL_CHAR('X', cells[origin].ch[0]);
    TEST_ASSERT_EQUAL_HEX32(foreground, cells[origin].fg);
    TEST_ASSERT_EQUAL_HEX32(output_background, cells[origin].bg);
    TEST_ASSERT_EQUAL_HEX16(TUI_STYLE_BOLD, cells[origin].style);
    TEST_ASSERT_EQUAL_CHAR('X', cells[origin + 1u].ch[0]);
    TEST_ASSERT_EQUAL_HEX32(
        output_background,
        cells[origin + 1u].bg);
    TEST_ASSERT_EQUAL_CHAR('X', cells[origin + 2u].ch[0]);
    TEST_ASSERT_EQUAL_HEX32(
        draft_background,
        cells[origin + 2u].bg);

    TuiCell snapshot[FRAME_WIDTH * FRAME_HEIGHT];
    memcpy(snapshot, cells, sizeof(snapshot));
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_render(&state, &target));
    TEST_ASSERT_EQUAL_MEMORY(snapshot, cells, sizeof(cells));

    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_finalize_stroke(&state));
    TEST_ASSERT_TRUE(canvas_state_can_undo(&state));
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_undo(&state));
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_render(&state, &target));
    TEST_ASSERT_EQUAL_CHAR(' ', cells[origin].ch[0]);
    TEST_ASSERT_EQUAL_HEX32(output_background, cells[origin].bg);

    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_redo(&state));
    TEST_ASSERT_EQUAL_INT(TG_OK, canvas_state_render(&state, &target));
    TEST_ASSERT_EQUAL_CHAR('X', cells[origin].ch[0]);
    canvas_state_destroy(&state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_output_bounds_and_init_validation);
    RUN_TEST(test_history_branching_and_reset);
    RUN_TEST(test_viewport_projection_boundaries);
    RUN_TEST(test_idle_and_null_state_contract);
    RUN_TEST(test_begin_cancel_and_reset_preserve_storage);
    RUN_TEST(test_undo_redo_finalize_pending_and_branch);
    RUN_TEST(test_bresenham_all_octants_and_duplicates);
    RUN_TEST(test_state_stroke_and_render);
    return UNITY_END();
}
