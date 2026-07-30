#include "canvas_state.h"

#include <assert.h>
#include <stddef.h>

typedef struct ReplayCapture {
    CanvasSample samples[16];
    size_t count;
} ReplayCapture;

static void capture_sample(
    const CanvasSample *sample,
    void *userdata)
{
    ReplayCapture *capture = userdata;
    assert(capture->count <
           sizeof(capture->samples) / sizeof(capture->samples[0]));
    capture->samples[capture->count++] = *sample;
}

static ReplayCapture replay(const CanvasDocument *document)
{
    ReplayCapture capture = {0};
    canvas_document_replay(document, capture_sample, &capture);
    return capture;
}

static CanvasSample sample_at(int x, int y, unsigned char ch)
{
    CanvasSample sample = {0};
    sample.position = (TgVec2i){x, y};
    sample.cell.ch[0] = (char)ch;
    sample.cell.width = 1;
    sample.cell.fg = TUI_COLOR_DEFAULT;
    sample.cell.bg = TUI_COLOR_DEFAULT;
    return sample;
}

static TuiCell cell_for(unsigned char ch)
{
    TuiCell cell = {0};
    cell.ch[0] = (char)ch;
    cell.width = 1;
    cell.fg = TUI_COLOR_DEFAULT;
    cell.bg = TUI_COLOR_DEFAULT;
    cell.style = TUI_STYLE_BOLD;
    return cell;
}

static void test_output_bounds(void)
{
    CanvasDocument odd;
    assert(canvas_document_init(&odd, (TgSizei){5, 3}) == TG_OK);
    assert(canvas_document_contains_output(&odd, (TgVec2i){-2, -1}));
    assert(canvas_document_contains_output(&odd, (TgVec2i){2, 1}));
    assert(!canvas_document_contains_output(&odd, (TgVec2i){-3, 0}));
    assert(!canvas_document_contains_output(&odd, (TgVec2i){3, 0}));
    canvas_document_destroy(&odd);

    CanvasDocument even;
    assert(canvas_document_init(&even, (TgSizei){4, 2}) == TG_OK);
    assert(canvas_document_contains_output(&even, (TgVec2i){-2, -1}));
    assert(canvas_document_contains_output(&even, (TgVec2i){1, 0}));
    assert(!canvas_document_contains_output(&even, (TgVec2i){2, 0}));
    assert(!canvas_document_contains_output(&even, (TgVec2i){0, 1}));
    canvas_document_destroy(&even);
}

static void test_history_and_branching(void)
{
    CanvasDocument document;
    assert(canvas_document_init(&document, (TgSizei){4, 2}) == TG_OK);

    CanvasSample first = sample_at(0, 0, 'A');
    CanvasSample draft = sample_at(20, 10, 'B');
    assert(canvas_document_commit_draw(&document, &first, 1) == TG_OK);
    assert(canvas_document_commit_draw(&document, &draft, 1) == TG_OK);
    assert(document.history.operation_count == 2);

    ReplayCapture capture = replay(&document);
    assert(capture.count == 2);
    assert(capture.samples[0].cell.ch[0] == 'A');
    assert(capture.samples[1].position.x == 20);

    assert(canvas_document_undo(&document));
    capture = replay(&document);
    assert(capture.count == 1);
    assert(canvas_document_can_redo(&document));

    assert(canvas_document_redo(&document));
    capture = replay(&document);
    assert(capture.count == 2);

    assert(canvas_document_undo(&document));
    CanvasSample branch = sample_at(1, 0, 'C');
    assert(canvas_document_commit_draw(&document, &branch, 1) == TG_OK);
    assert(!canvas_document_can_redo(&document));
    assert(document.history.operation_count == 2);

    capture = replay(&document);
    assert(capture.count == 2);
    assert(capture.samples[0].cell.ch[0] == 'A');
    assert(capture.samples[1].cell.ch[0] == 'C');

    canvas_document_reset(&document);
    assert(!canvas_document_can_undo(&document));
    assert(!canvas_document_can_redo(&document));
    assert(replay(&document).count == 0);
    canvas_document_destroy(&document);
}

static void test_viewport_projection(void)
{
    CanvasViewport viewport = {
        .rect = {2, 1, 7, 5},
        .origin_screen = {5, 3},
    };
    TgVec2i position;
    assert(canvas_viewport_screen_to_world(
        &viewport, 5, 3, &position));
    assert(position.x == 0 && position.y == 0);
    assert(canvas_viewport_screen_to_world(
        &viewport, 2, 1, &position));
    assert(position.x == -3 && position.y == -2);
    assert(!canvas_viewport_screen_to_world(
        &viewport, 9, 3, &position));

    TgVec2i screen;
    assert(canvas_viewport_world_to_screen(
        &viewport, (TgVec2i){0, 0}, &screen));
    assert(screen.x == 5 && screen.y == 3);
    assert(canvas_viewport_world_to_screen(
        &viewport, (TgVec2i){-3, -2}, &screen));
    assert(screen.x == 2 && screen.y == 1);
    assert(!canvas_viewport_world_to_screen(
        &viewport, (TgVec2i){4, 0}, &screen));
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
    TuiCell cells[FRAME_WIDTH * FRAME_HEIGHT] = {0};

    CanvasState state;
    assert(canvas_state_init(&state, (TgSizei){3, 3}) == TG_OK);
    assert(canvas_state_begin_stroke(
        &state,
        (TgVec2i){0, 0},
        cell_for('X')) == TG_OK);
    assert(canvas_state_append_stroke(
        &state,
        (TgVec2i){2, 0}) == TG_OK);
    assert(canvas_state_stroke_active(&state));
    assert(state.pending_stroke.sample_count == 3);

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
    assert(canvas_state_render(&state, &target) == TG_OK);

    size_t origin =
        (size_t)target.viewport.origin_screen.y * FRAME_WIDTH +
        (size_t)target.viewport.origin_screen.x;
    assert(cells[origin].ch[0] == 'X');
    assert(cells[origin].fg == foreground);
    assert(cells[origin].bg == output_background);
    assert(cells[origin].style == TUI_STYLE_BOLD);
    assert(cells[origin + 1u].ch[0] == 'X');
    assert(cells[origin + 1u].bg == output_background);
    assert(cells[origin + 2u].ch[0] == 'X');
    assert(cells[origin + 2u].bg == draft_background);

    assert(canvas_state_finalize_stroke(&state) == TG_OK);
    assert(!canvas_state_stroke_active(&state));
    assert(canvas_state_can_undo(&state));
    assert(state.document.history.operation_count == 1);

    assert(canvas_state_undo(&state) == TG_OK);
    assert(canvas_state_can_redo(&state));
    assert(canvas_state_render(&state, &target) == TG_OK);
    assert(cells[origin].ch[0] == ' ');
    assert(cells[origin].bg == output_background);

    assert(canvas_state_redo(&state) == TG_OK);
    assert(canvas_state_render(&state, &target) == TG_OK);
    assert(cells[origin].ch[0] == 'X');

    canvas_state_reset(&state);
    assert(!canvas_state_can_undo(&state));
    assert(!canvas_state_stroke_active(&state));
    canvas_state_destroy(&state);
}

int main(void)
{
    test_output_bounds();
    test_history_and_branching();
    test_viewport_projection();
    test_state_stroke_and_render();
    return 0;
}
