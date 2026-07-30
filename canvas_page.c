#include "canvas_page.h"

#include "canvas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CANVAS_PALETTE_FIRST 32
#define CANVAS_PALETTE_LAST 126
#define CANVAS_PALETTE_COUNT \
    ((size_t)(CANVAS_PALETTE_LAST - CANVAS_PALETTE_FIRST + 1))

#define CANVAS_COLOR_PAGE_BG 0x0f1419u
#define CANVAS_COLOR_PANEL_BG 0x151c23u
#define CANVAS_COLOR_DRAFT_BG 0x1a232cu
#define CANVAS_COLOR_OUTPUT_BG 0x222e39u
#define CANVAS_COLOR_BORDER 0x596875u
#define CANVAS_COLOR_TEXT 0xd7e0e7u
#define CANVAS_COLOR_MUTED 0x82909cu
#define CANVAS_COLOR_ACCENT 0x4f8fcdu
#define CANVAS_COLOR_DISABLED 0x3d4852u

typedef enum CanvasButton {
    CANVAS_BUTTON_NEW = 0,
    CANVAS_BUTTON_SAVE,
    CANVAS_BUTTON_UNDO,
    CANVAS_BUTTON_REDO,
    CANVAS_BUTTON_COUNT
} CanvasButton;

typedef struct CanvasPalette {
    unsigned char items[CANVAS_PALETTE_COUNT];
    size_t selected_index;
    int scroll_row;
} CanvasPalette;

typedef struct CanvasLayout {
    TgSizei frame_size;
    TgRecti toolbox_panel;
    TgRecti canvas_panel;
    TgRecti canvas_view;
    TgRecti palette_panel;
    TgRecti palette_view;
    TgRecti toolbar;
    TgRecti buttons[CANVAS_BUTTON_COUNT];
    TgVec2i origin_screen;
    int palette_columns;
    int palette_visible_rows;
} CanvasLayout;

typedef struct CanvasStroke {
    bool active;
    unsigned char ch;
    CanvasSample *samples;
    size_t sample_count;
    size_t sample_capacity;
} CanvasStroke;

struct CanvasPage {
    CanvasDocument document;
    CanvasPalette palette;
    CanvasLayout layout;
    CanvasStroke stroke;
    TgVec2i last_mouse_world;
    bool last_mouse_on_canvas;
    bool layout_dirty;
    bool frame_dirty;
    char status[96];
};

typedef struct CanvasReplayContext {
    CanvasPage *state;
    AppFrame *frame;
} CanvasReplayContext;

static bool canvas_rect_contains(TgRecti rect, int x, int y)
{
    return rect.w > 0 &&
           rect.h > 0 &&
           x >= rect.x &&
           y >= rect.y &&
           (int64_t)x < (int64_t)rect.x + rect.w &&
           (int64_t)y < (int64_t)rect.y + rect.h;
}

static TuiCell canvas_cell(
    unsigned char ch,
    uint32_t fg,
    uint32_t bg,
    uint16_t style)
{
    TuiCell cell;
    memset(&cell, 0, sizeof(cell));
    cell.ch[0] = (char)ch;
    cell.width = 1;
    cell.fg = fg;
    cell.bg = bg;
    cell.style = style;
    return cell;
}

static void canvas_frame_put(
    AppFrame *frame,
    int x,
    int y,
    unsigned char ch,
    uint32_t fg,
    uint32_t bg,
    uint16_t style)
{
    if (frame == NULL ||
        frame->cells == NULL ||
        x < 0 ||
        y < 0 ||
        x >= frame->size.w ||
        y >= frame->size.h) {
        return;
    }

    size_t index = (size_t)y * (size_t)frame->size.w + (size_t)x;
    frame->cells[index] = canvas_cell(ch, fg, bg, style);
}

static void canvas_frame_fill(
    AppFrame *frame,
    TgRecti rect,
    uint32_t fg,
    uint32_t bg,
    uint16_t style)
{
    if (frame == NULL || frame->cells == NULL) {
        return;
    }

    int x_start = rect.x < 0 ? 0 : rect.x;
    int y_start = rect.y < 0 ? 0 : rect.y;
    int64_t raw_x_end = (int64_t)rect.x + rect.w;
    int64_t raw_y_end = (int64_t)rect.y + rect.h;
    int x_end = raw_x_end > frame->size.w
        ? frame->size.w
        : (int)raw_x_end;
    int y_end = raw_y_end > frame->size.h
        ? frame->size.h
        : (int)raw_y_end;

    for (int y = y_start; y < y_end; ++y) {
        for (int x = x_start; x < x_end; ++x) {
            canvas_frame_put(frame, x, y, ' ', fg, bg, style);
        }
    }
}

static void canvas_frame_text(
    AppFrame *frame,
    int x,
    int y,
    const char *text,
    uint32_t fg,
    uint32_t bg,
    uint16_t style)
{
    if (text == NULL) {
        return;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        canvas_frame_put(
            frame,
            x + (int)i,
            y,
            (unsigned char)text[i],
            fg,
            bg,
            style);
    }
}

static void canvas_frame_box(
    AppFrame *frame,
    TgRecti rect,
    const char *title)
{
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    canvas_frame_fill(
        frame,
        rect,
        CANVAS_COLOR_TEXT,
        CANVAS_COLOR_PANEL_BG,
        TUI_STYLE_NONE);

    for (int x = rect.x; x < rect.x + rect.w; ++x) {
        canvas_frame_put(
            frame,
            x,
            rect.y,
            '-',
            CANVAS_COLOR_BORDER,
            CANVAS_COLOR_PANEL_BG,
            TUI_STYLE_NONE);
        canvas_frame_put(
            frame,
            x,
            rect.y + rect.h - 1,
            '-',
            CANVAS_COLOR_BORDER,
            CANVAS_COLOR_PANEL_BG,
            TUI_STYLE_NONE);
    }
    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        canvas_frame_put(
            frame,
            rect.x,
            y,
            '|',
            CANVAS_COLOR_BORDER,
            CANVAS_COLOR_PANEL_BG,
            TUI_STYLE_NONE);
        canvas_frame_put(
            frame,
            rect.x + rect.w - 1,
            y,
            '|',
            CANVAS_COLOR_BORDER,
            CANVAS_COLOR_PANEL_BG,
            TUI_STYLE_NONE);
    }

    canvas_frame_put(
        frame,
        rect.x,
        rect.y,
        '+',
        CANVAS_COLOR_BORDER,
        CANVAS_COLOR_PANEL_BG,
        TUI_STYLE_NONE);
    canvas_frame_put(
        frame,
        rect.x + rect.w - 1,
        rect.y,
        '+',
        CANVAS_COLOR_BORDER,
        CANVAS_COLOR_PANEL_BG,
        TUI_STYLE_NONE);
    canvas_frame_put(
        frame,
        rect.x,
        rect.y + rect.h - 1,
        '+',
        CANVAS_COLOR_BORDER,
        CANVAS_COLOR_PANEL_BG,
        TUI_STYLE_NONE);
    canvas_frame_put(
        frame,
        rect.x + rect.w - 1,
        rect.y + rect.h - 1,
        '+',
        CANVAS_COLOR_BORDER,
        CANVAS_COLOR_PANEL_BG,
        TUI_STYLE_NONE);

    if (title != NULL && rect.w > 4) {
        canvas_frame_text(
            frame,
            rect.x + 2,
            rect.y,
            title,
            CANVAS_COLOR_TEXT,
            CANVAS_COLOR_PANEL_BG,
            TUI_STYLE_BOLD);
    }
}

static void canvas_layout_compute(CanvasPage *state, TgSizei frame_size)
{
    CanvasLayout *layout = &state->layout;
    memset(layout, 0, sizeof(*layout));
    layout->frame_size = frame_size;

    int toolbar_height = frame_size.h > 1 ? 1 : 0;
    int workspace_height = frame_size.h - toolbar_height;
    int toolbox_width = frame_size.w >= 70 ? 18 : 0;
    int palette_width = frame_size.w >= 40 ? 24 : 0;
    int gutters = (toolbox_width > 0 ? 1 : 0) +
                  (palette_width > 0 ? 1 : 0);
    int canvas_width =
        frame_size.w - toolbox_width - palette_width - gutters;

    if (canvas_width < 3 && palette_width > 0) {
        palette_width = 0;
        --gutters;
        canvas_width =
            frame_size.w - toolbox_width - palette_width - gutters;
    }
    if (canvas_width < 3 && toolbox_width > 0) {
        toolbox_width = 0;
        --gutters;
        canvas_width =
            frame_size.w - toolbox_width - palette_width - gutters;
    }

    int x = 0;
    if (toolbox_width > 0) {
        layout->toolbox_panel =
            (TgRecti){x, 0, toolbox_width, workspace_height};
        x += toolbox_width + 1;
    }

    layout->canvas_panel =
        (TgRecti){x, 0, canvas_width, workspace_height};
    if (layout->canvas_panel.w > 2 && layout->canvas_panel.h > 2) {
        layout->canvas_view = (TgRecti){
            layout->canvas_panel.x + 1,
            layout->canvas_panel.y + 1,
            layout->canvas_panel.w - 2,
            layout->canvas_panel.h - 2,
        };
    }
    x += canvas_width + (palette_width > 0 ? 1 : 0);

    if (palette_width > 0) {
        layout->palette_panel =
            (TgRecti){x, 0, palette_width, workspace_height};
        if (palette_width > 2 && workspace_height > 2) {
            layout->palette_view = (TgRecti){
                x + 1,
                1,
                palette_width - 2,
                workspace_height - 2,
            };
        }
    }

    layout->toolbar =
        (TgRecti){0, workspace_height, frame_size.w, toolbar_height};
    layout->origin_screen = (TgVec2i){
        layout->canvas_view.x + layout->canvas_view.w / 2,
        layout->canvas_view.y + layout->canvas_view.h / 2,
    };

    layout->palette_columns = layout->palette_view.w / 2;
    if (layout->palette_columns < 1) {
        layout->palette_columns = 1;
    }
    layout->palette_visible_rows = layout->palette_view.h;

    static const int widths[CANVAS_BUTTON_COUNT] = {7, 8, 8, 8};
    int button_x = 0;
    for (int i = 0; i < CANVAS_BUTTON_COUNT; ++i) {
        layout->buttons[i] = (TgRecti){
            button_x,
            layout->toolbar.y,
            widths[i],
            layout->toolbar.h,
        };
        button_x += widths[i];
    }

    state->layout_dirty = false;
    state->frame_dirty = true;
}

static bool canvas_screen_to_world(
    const CanvasPage *state,
    int screen_x,
    int screen_y,
    TgVec2i *out_position)
{
    if (state == NULL ||
        out_position == NULL ||
        !canvas_rect_contains(
            state->layout.canvas_view,
            screen_x,
            screen_y)) {
        return false;
    }

    out_position->x = screen_x - state->layout.origin_screen.x;
    out_position->y = screen_y - state->layout.origin_screen.y;
    return true;
}

static bool canvas_world_to_screen(
    const CanvasPage *state,
    TgVec2i position,
    TgVec2i *out_screen)
{
    if (state == NULL || out_screen == NULL) {
        return false;
    }

    int64_t screen_x =
        (int64_t)state->layout.origin_screen.x + position.x;
    int64_t screen_y =
        (int64_t)state->layout.origin_screen.y + position.y;
    if (screen_x < INT32_MIN ||
        screen_x > INT32_MAX ||
        screen_y < INT32_MIN ||
        screen_y > INT32_MAX ||
        !canvas_rect_contains(
            state->layout.canvas_view,
            (int)screen_x,
            (int)screen_y)) {
        return false;
    }

    out_screen->x = (int32_t)screen_x;
    out_screen->y = (int32_t)screen_y;
    return true;
}

static int canvas_palette_total_rows(const CanvasPage *state)
{
    int columns = state->layout.palette_columns;
    return (int)((CANVAS_PALETTE_COUNT + (size_t)columns - 1u) /
                 (size_t)columns);
}

static void canvas_palette_clamp_scroll(CanvasPage *state)
{
    int max_scroll =
        canvas_palette_total_rows(state) -
        state->layout.palette_visible_rows;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (state->palette.scroll_row < 0) {
        state->palette.scroll_row = 0;
    }
    if (state->palette.scroll_row > max_scroll) {
        state->palette.scroll_row = max_scroll;
    }
}

static void canvas_palette_reveal_selection(CanvasPage *state)
{
    if (state->layout.palette_visible_rows <= 0) {
        return;
    }

    int selected_row =
        (int)(state->palette.selected_index /
              (size_t)state->layout.palette_columns);
    if (selected_row < state->palette.scroll_row) {
        state->palette.scroll_row = selected_row;
    }
    int last_visible =
        state->palette.scroll_row +
        state->layout.palette_visible_rows - 1;
    if (selected_row > last_visible) {
        state->palette.scroll_row =
            selected_row - state->layout.palette_visible_rows + 1;
    }
    canvas_palette_clamp_scroll(state);
}

static void canvas_palette_select(CanvasPage *state, unsigned char ch)
{
    if (ch < CANVAS_PALETTE_FIRST || ch > CANVAS_PALETTE_LAST) {
        return;
    }

    state->palette.selected_index =
        (size_t)(ch - CANVAS_PALETTE_FIRST);
    canvas_palette_reveal_selection(state);
    state->frame_dirty = true;
    if (ch == ' ') {
        (void)snprintf(
            state->status,
            sizeof(state->status),
            "Selected: SPACE (32)");
    } else {
        (void)snprintf(
            state->status,
            sizeof(state->status),
            "Selected: %c (%u)",
            ch,
            (unsigned)ch);
    }
}

static unsigned char canvas_palette_selected(const CanvasPage *state)
{
    return state->palette.items[state->palette.selected_index];
}

static bool canvas_palette_hit(
    const CanvasPage *state,
    int x,
    int y,
    size_t *out_index)
{
    if (state == NULL ||
        out_index == NULL ||
        !canvas_rect_contains(state->layout.palette_view, x, y)) {
        return false;
    }

    int column = (x - state->layout.palette_view.x) / 2;
    int visible_row = y - state->layout.palette_view.y;
    int row = state->palette.scroll_row + visible_row;
    size_t index =
        (size_t)row * (size_t)state->layout.palette_columns +
        (size_t)column;
    if (index >= CANVAS_PALETTE_COUNT) {
        return false;
    }

    *out_index = index;
    return true;
}

static TgResult canvas_stroke_reserve(
    CanvasStroke *stroke,
    size_t required)
{
    if (required <= stroke->sample_capacity) {
        return TG_OK;
    }

    size_t capacity =
        stroke->sample_capacity == 0 ? 32u : stroke->sample_capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            return TG_ERR_NOMEM;
        }
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(*stroke->samples)) {
        return TG_ERR_NOMEM;
    }

    CanvasSample *samples =
        realloc(stroke->samples, capacity * sizeof(*samples));
    if (samples == NULL) {
        return TG_ERR_NOMEM;
    }

    stroke->samples = samples;
    stroke->sample_capacity = capacity;
    return TG_OK;
}

static TgResult canvas_stroke_append_raw(
    CanvasStroke *stroke,
    TgVec2i position)
{
    if (stroke->sample_count > 0) {
        CanvasSample *last = &stroke->samples[stroke->sample_count - 1u];
        if (last->position.x == position.x &&
            last->position.y == position.y) {
            return TG_OK;
        }
    }

    TgResult result =
        canvas_stroke_reserve(stroke, stroke->sample_count + 1u);
    if (tg_result_err(result)) {
        return result;
    }

    CanvasSample sample;
    memset(&sample, 0, sizeof(sample));
    sample.position = position;
    sample.cell.ch[0] = (char)stroke->ch;
    sample.cell.width = 1;
    sample.cell.fg = TUI_COLOR_DEFAULT;
    sample.cell.bg = TUI_COLOR_DEFAULT;
    sample.cell.style = TUI_STYLE_NONE;
    stroke->samples[stroke->sample_count++] = sample;
    return TG_OK;
}

static TgResult canvas_stroke_append(
    CanvasStroke *stroke,
    TgVec2i position)
{
    if (stroke->sample_count == 0) {
        return canvas_stroke_append_raw(stroke, position);
    }

    TgVec2i last = stroke->samples[stroke->sample_count - 1u].position;
    int x = last.x;
    int y = last.y;
    int dx = position.x > x ? position.x - x : x - position.x;
    int sx = x < position.x ? 1 : -1;
    int dy_abs = position.y > y ? position.y - y : y - position.y;
    int dy = -dy_abs;
    int sy = y < position.y ? 1 : -1;
    int error = dx + dy;

    while (x != position.x || y != position.y) {
        int twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y += sy;
        }

        TgResult result = canvas_stroke_append_raw(
            stroke,
            (TgVec2i){x, y});
        if (tg_result_err(result)) {
            return result;
        }
    }
    return TG_OK;
}

static TgResult canvas_stroke_begin(
    CanvasPage *state,
    TgVec2i position)
{
    state->stroke.active = true;
    state->stroke.ch = canvas_palette_selected(state);
    state->stroke.sample_count = 0;
    TgResult result = canvas_stroke_append(&state->stroke, position);
    if (tg_result_ok(result)) {
        state->frame_dirty = true;
    }
    return result;
}

static TgResult canvas_stroke_finalize(CanvasPage *state)
{
    if (!state->stroke.active) {
        return TG_OK;
    }

    TgResult result = canvas_document_commit_draw(
        &state->document,
        state->stroke.samples,
        state->stroke.sample_count);
    if (tg_result_err(result)) {
        return result;
    }

    state->stroke.active = false;
    state->stroke.sample_count = 0;
    state->frame_dirty = true;
    return TG_OK;
}

static void canvas_stroke_cancel(CanvasPage *state)
{
    state->stroke.active = false;
    state->stroke.sample_count = 0;
    state->frame_dirty = true;
}

static TgResult canvas_page_new_document(CanvasPage *state)
{
    canvas_stroke_cancel(state);
    canvas_document_reset(&state->document);
    (void)snprintf(
        state->status,
        sizeof(state->status),
        "New canvas");
    state->frame_dirty = true;
    return TG_OK;
}

static TgResult canvas_page_undo(CanvasPage *state)
{
    TgResult result = canvas_stroke_finalize(state);
    if (tg_result_err(result)) {
        return result;
    }
    if (canvas_document_undo(&state->document)) {
        (void)snprintf(state->status, sizeof(state->status), "Undo");
        state->frame_dirty = true;
    }
    return TG_OK;
}

static TgResult canvas_page_redo(CanvasPage *state)
{
    TgResult result = canvas_stroke_finalize(state);
    if (tg_result_err(result)) {
        return result;
    }
    if (canvas_document_redo(&state->document)) {
        (void)snprintf(state->status, sizeof(state->status), "Redo");
        state->frame_dirty = true;
    }
    return TG_OK;
}

static TgResult canvas_page_activate_button(
    CanvasPage *state,
    CanvasButton button)
{
    switch (button) {
    case CANVAS_BUTTON_NEW:
        return canvas_page_new_document(state);
    case CANVAS_BUTTON_SAVE:
        (void)snprintf(
            state->status,
            sizeof(state->status),
            "Save is not implemented yet");
        state->frame_dirty = true;
        return TG_OK;
    case CANVAS_BUTTON_UNDO:
        return canvas_page_undo(state);
    case CANVAS_BUTTON_REDO:
        return canvas_page_redo(state);
    default:
        return TG_ERR_INVALID;
    }
}

static TgResult canvas_page_handle_mouse(
    CanvasPage *state,
    const TuiMouseEvent *mouse)
{
    if (mouse->action == TUI_MOUSE_WHEEL &&
        canvas_rect_contains(
            state->layout.palette_panel,
            mouse->x,
            mouse->y)) {
        state->palette.scroll_row -= mouse->wheel_y;
        canvas_palette_clamp_scroll(state);
        state->frame_dirty = true;
        return TG_OK;
    }

    if (mouse->button != TUI_MOUSE_LEFT) {
        return TG_OK;
    }

    if (mouse->action == TUI_MOUSE_PRESS) {
        size_t palette_index = 0;
        if (canvas_palette_hit(
                state,
                mouse->x,
                mouse->y,
                &palette_index)) {
            TgResult result = canvas_stroke_finalize(state);
            if (tg_result_err(result)) {
                return result;
            }
            canvas_palette_select(
                state,
                state->palette.items[palette_index]);
            return TG_OK;
        }

        for (int i = 0; i < CANVAS_BUTTON_COUNT; ++i) {
            if (canvas_rect_contains(
                    state->layout.buttons[i],
                    mouse->x,
                    mouse->y)) {
                return canvas_page_activate_button(
                    state,
                    (CanvasButton)i);
            }
        }

        TgVec2i position;
        if (canvas_screen_to_world(
                state,
                mouse->x,
                mouse->y,
                &position)) {
            state->last_mouse_world = position;
            state->last_mouse_on_canvas = true;
            return canvas_stroke_begin(state, position);
        }
        return TG_OK;
    }

    if (mouse->action == TUI_MOUSE_DRAG && state->stroke.active) {
        TgVec2i position;
        if (canvas_screen_to_world(
                state,
                mouse->x,
                mouse->y,
                &position)) {
            state->last_mouse_world = position;
            state->last_mouse_on_canvas = true;
            TgResult result =
                canvas_stroke_append(&state->stroke, position);
            if (tg_result_ok(result)) {
                state->frame_dirty = true;
            }
            return result;
        }
        state->last_mouse_on_canvas = false;
        return TG_OK;
    }

    if (mouse->action == TUI_MOUSE_RELEASE) {
        return canvas_stroke_finalize(state);
    }
    return TG_OK;
}

static TgResult canvas_page_on_enter(Page *page)
{
    if (page == NULL || page->userdata == NULL) {
        return TG_ERR_INVALID;
    }

    CanvasPage *state = page->userdata;
    canvas_layout_compute(state, page->frame.size);
    canvas_palette_reveal_selection(state);
    state->frame_dirty = true;
    return TG_OK;
}

static TgResult canvas_page_on_leave(
    Page *page,
    PageLeaveReason reason)
{
    (void)reason;
    if (page == NULL || page->userdata == NULL) {
        return TG_ERR_INVALID;
    }
    return canvas_stroke_finalize(page->userdata);
}

static TgResult canvas_page_handle_event(
    Page *page,
    const TuiInputEvent *event)
{
    if (page == NULL || page->userdata == NULL || event == NULL) {
        return TG_ERR_INVALID;
    }

    CanvasPage *state = page->userdata;
    if (event->type == TUI_INPUT_KEY &&
        (event->modifiers & TUI_MOD_CONTROL) != 0) {
        switch (event->ch) {
        case 'n':
            return canvas_page_new_document(state);
        case 's':
            return canvas_page_activate_button(
                state,
                CANVAS_BUTTON_SAVE);
        case 'y':
            return canvas_page_redo(state);
        case 'z':
            return canvas_page_undo(state);
        default:
            return TG_OK;
        }
    }

    if (event->type == TUI_INPUT_TEXT &&
        event->ch >= CANVAS_PALETTE_FIRST &&
        event->ch <= CANVAS_PALETTE_LAST) {
        TgResult result = canvas_stroke_finalize(state);
        if (tg_result_err(result)) {
            return result;
        }
        canvas_palette_select(state, event->ch);
        return TG_OK;
    }

    if (event->type == TUI_INPUT_MOUSE) {
        return canvas_page_handle_mouse(state, &event->mouse);
    }
    return TG_OK;
}

static TgResult canvas_page_update(
    Page *page,
    const AppFrameContext *context)
{
    (void)context;
    if (page == NULL || page->userdata == NULL) {
        return TG_ERR_INVALID;
    }

    CanvasPage *state = page->userdata;
    if (state->layout.frame_size.w != page->frame.size.w ||
        state->layout.frame_size.h != page->frame.size.h ||
        state->layout_dirty) {
        canvas_layout_compute(state, page->frame.size);
        canvas_palette_reveal_selection(state);
    }
    return TG_OK;
}

static void canvas_page_draw_sample(
    const CanvasSample *sample,
    void *userdata)
{
    CanvasReplayContext *context = userdata;
    TgVec2i screen;
    if (!canvas_world_to_screen(
            context->state,
            sample->position,
            &screen)) {
        return;
    }

    uint32_t bg = canvas_document_contains_output(
        &context->state->document,
        sample->position)
        ? CANVAS_COLOR_OUTPUT_BG
        : CANVAS_COLOR_DRAFT_BG;
    uint32_t fg = sample->cell.fg == TUI_COLOR_DEFAULT
        ? CANVAS_COLOR_TEXT
        : sample->cell.fg;
    if (sample->cell.bg != TUI_COLOR_DEFAULT) {
        bg = sample->cell.bg;
    }
    canvas_frame_put(
        context->frame,
        screen.x,
        screen.y,
        (unsigned char)sample->cell.ch[0],
        fg,
        bg,
        sample->cell.style);
}

static void canvas_page_draw_canvas(
    CanvasPage *state,
    AppFrame *frame)
{
    canvas_frame_box(frame, state->layout.canvas_panel, "Canvas");

    TgRecti view = state->layout.canvas_view;
    for (int y = view.y; y < view.y + view.h; ++y) {
        for (int x = view.x; x < view.x + view.w; ++x) {
            TgVec2i position = {
                x - state->layout.origin_screen.x,
                y - state->layout.origin_screen.y,
            };
            uint32_t bg = canvas_document_contains_output(
                &state->document,
                position)
                ? CANVAS_COLOR_OUTPUT_BG
                : CANVAS_COLOR_DRAFT_BG;
            canvas_frame_put(
                frame,
                x,
                y,
                ' ',
                CANVAS_COLOR_TEXT,
                bg,
                TUI_STYLE_NONE);
        }
    }

    CanvasReplayContext context = {
        .state = state,
        .frame = frame,
    };
    canvas_document_replay(
        &state->document,
        canvas_page_draw_sample,
        &context);
    for (size_t i = 0; i < state->stroke.sample_count; ++i) {
        canvas_page_draw_sample(&state->stroke.samples[i], &context);
    }
}

static void canvas_page_draw_toolbox(
    CanvasPage *state,
    AppFrame *frame)
{
    if (state->layout.toolbox_panel.w <= 0) {
        return;
    }

    canvas_frame_box(frame, state->layout.toolbox_panel, "Tool Box");
    canvas_frame_text(
        frame,
        state->layout.toolbox_panel.x + 2,
        state->layout.toolbox_panel.y + 2,
        "Draw",
        CANVAS_COLOR_TEXT,
        CANVAS_COLOR_ACCENT,
        TUI_STYLE_BOLD);
    canvas_frame_text(
        frame,
        state->layout.toolbox_panel.x + 2,
        state->layout.toolbox_panel.y + 4,
        "More later",
        CANVAS_COLOR_MUTED,
        CANVAS_COLOR_PANEL_BG,
        TUI_STYLE_DIM);
}

static void canvas_page_draw_palette(
    CanvasPage *state,
    AppFrame *frame)
{
    if (state->layout.palette_panel.w <= 0) {
        return;
    }

    canvas_frame_box(frame, state->layout.palette_panel, "Palette");
    int columns = state->layout.palette_columns;
    int first_row = state->palette.scroll_row;
    int last_row =
        first_row + state->layout.palette_visible_rows;

    for (size_t index = 0; index < CANVAS_PALETTE_COUNT; ++index) {
        int row = (int)(index / (size_t)columns);
        if (row < first_row || row >= last_row) {
            continue;
        }
        int column = (int)(index % (size_t)columns);
        int x = state->layout.palette_view.x + column * 2;
        int y =
            state->layout.palette_view.y + row - first_row;
        bool selected = index == state->palette.selected_index;
        uint32_t bg = selected
            ? CANVAS_COLOR_ACCENT
            : CANVAS_COLOR_PANEL_BG;
        uint16_t style = selected ? TUI_STYLE_BOLD : TUI_STYLE_NONE;
        unsigned char ch = state->palette.items[index];

        if (ch == ' ') {
            canvas_frame_put(
                frame, x, y, 'S',
                CANVAS_COLOR_TEXT, bg, style);
            canvas_frame_put(
                frame, x + 1, y, 'P',
                CANVAS_COLOR_TEXT, bg, style);
        } else {
            canvas_frame_put(
                frame, x, y, ch,
                CANVAS_COLOR_TEXT, bg, style);
            canvas_frame_put(
                frame, x + 1, y, ' ',
                CANVAS_COLOR_TEXT, bg, style);
        }
    }
}

static void canvas_page_draw_button(
    CanvasPage *state,
    AppFrame *frame,
    CanvasButton button,
    const char *label,
    bool enabled)
{
    TgRecti rect = state->layout.buttons[button];
    if (rect.h <= 0 || rect.w <= 0) {
        return;
    }

    uint32_t fg = enabled
        ? CANVAS_COLOR_TEXT
        : CANVAS_COLOR_DISABLED;
    uint16_t style = enabled ? TUI_STYLE_BOLD : TUI_STYLE_DIM;
    canvas_frame_fill(
        frame,
        rect,
        fg,
        CANVAS_COLOR_PANEL_BG,
        style);
    canvas_frame_text(
        frame,
        rect.x + 1,
        rect.y,
        label,
        fg,
        CANVAS_COLOR_PANEL_BG,
        style);
}

static void canvas_page_draw_toolbar(
    CanvasPage *state,
    AppFrame *frame)
{
    if (state->layout.toolbar.h <= 0) {
        return;
    }

    canvas_frame_fill(
        frame,
        state->layout.toolbar,
        CANVAS_COLOR_TEXT,
        CANVAS_COLOR_PANEL_BG,
        TUI_STYLE_NONE);
    canvas_page_draw_button(
        state, frame, CANVAS_BUTTON_NEW, "New", true);
    canvas_page_draw_button(
        state, frame, CANVAS_BUTTON_SAVE, "Save", false);
    canvas_page_draw_button(
        state,
        frame,
        CANVAS_BUTTON_UNDO,
        "Undo",
        canvas_document_can_undo(&state->document));
    canvas_page_draw_button(
        state,
        frame,
        CANVAS_BUTTON_REDO,
        "Redo",
        canvas_document_can_redo(&state->document));

    int status_x = state->layout.buttons[CANVAS_BUTTON_REDO].x +
                   state->layout.buttons[CANVAS_BUTTON_REDO].w + 1;
    if (status_x < frame->size.w) {
        canvas_frame_text(
            frame,
            status_x,
            state->layout.toolbar.y,
            state->status,
            CANVAS_COLOR_MUTED,
            CANVAS_COLOR_PANEL_BG,
            TUI_STYLE_DIM);
    }
}

static TgResult canvas_page_render(Page *page)
{
    if (page == NULL || page->userdata == NULL) {
        return TG_ERR_INVALID;
    }

    CanvasPage *state = page->userdata;
    if (!state->frame_dirty) {
        return TG_OK;
    }

    canvas_frame_fill(
        &page->frame,
        (TgRecti){0, 0, page->frame.size.w, page->frame.size.h},
        CANVAS_COLOR_TEXT,
        CANVAS_COLOR_PAGE_BG,
        TUI_STYLE_NONE);
    canvas_page_draw_toolbox(state, &page->frame);
    canvas_page_draw_canvas(state, &page->frame);
    canvas_page_draw_palette(state, &page->frame);
    canvas_page_draw_toolbar(state, &page->frame);
    state->frame_dirty = false;
    return TG_OK;
}

static void canvas_page_destroy(Page *page)
{
    if (page == NULL || page->userdata == NULL) {
        return;
    }

    CanvasPage *state = page->userdata;
    free(state->stroke.samples);
    state->stroke.samples = NULL;
    canvas_document_destroy(&state->document);
    free(state);
    page->userdata = NULL;
}

TgResult canvas_page_create(
    TgSizei output_size,
    CanvasPage **out_page)
{
    if (out_page == NULL) {
        return TG_ERR_INVALID;
    }
    *out_page = NULL;

    CanvasPage *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        return TG_ERR_NOMEM;
    }

    TgResult result =
        canvas_document_init(&state->document, output_size);
    if (tg_result_err(result)) {
        free(state);
        return result;
    }

    for (size_t i = 0; i < CANVAS_PALETTE_COUNT; ++i) {
        state->palette.items[i] =
            (unsigned char)(CANVAS_PALETTE_FIRST + (int)i);
    }
    state->palette.selected_index =
        (size_t)('#' - CANVAS_PALETTE_FIRST);
    state->layout_dirty = true;
    state->frame_dirty = true;
    (void)snprintf(
        state->status,
        sizeof(state->status),
        "Selected: # (35)");

    *out_page = state;
    return TG_OK;
}

PageOps canvas_page_ops(void)
{
    return (PageOps){
        .on_enter = canvas_page_on_enter,
        .on_leave = canvas_page_on_leave,
        .handle_event = canvas_page_handle_event,
        .update = canvas_page_update,
        .render = canvas_page_render,
        .destroy = canvas_page_destroy,
    };
}
