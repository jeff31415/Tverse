#include "canvas_frame.h"

#include <string.h>

#define CANVAS_COLOR_PANEL_BG 0x151c23u
#define CANVAS_COLOR_BORDER 0x596875u
#define CANVAS_COLOR_TEXT 0xd7e0e7u

static TuiCell canvas_frame_cell(
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

void canvas_frame_put(
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
    frame->cells[index] = canvas_frame_cell(ch, fg, bg, style);
}

void canvas_frame_fill(
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

void canvas_frame_text(
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

void canvas_frame_box(
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
