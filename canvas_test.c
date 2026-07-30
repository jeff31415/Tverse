#include "canvas.h"

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

int main(void)
{
    test_output_bounds();
    test_history_and_branching();
    return 0;
}
