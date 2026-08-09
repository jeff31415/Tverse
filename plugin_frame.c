#include "plugin_frame.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define PLUGIN_FRAME_COLOR_PANEL_BG 0x151c23u
#define PLUGIN_FRAME_COLOR_BORDER 0x596875u
#define PLUGIN_FRAME_COLOR_TEXT 0xd7e0e7u

static TuiCell plugin_frame_cell(
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

void plugin_frame_clear(DrawPluginSurface *surface)
{
    if (surface == NULL || surface->cells == NULL ||
        surface->size.w <= 0 || surface->size.h <= 0) {
        return;
    }

    size_t width = (size_t)surface->size.w;
    size_t height = (size_t)surface->size.h;
    if (width > SIZE_MAX / height) {
        return;
    }

    TuiCell cell = plugin_frame_cell(
        ' ', TUI_COLOR_DEFAULT, TUI_COLOR_DEFAULT, TUI_STYLE_NONE);
    size_t count = width * height;
    for (size_t i = 0; i < count; ++i) {
        surface->cells[i] = cell;
    }
}

void plugin_frame_put(
    DrawPluginSurface *surface,
    int x,
    int y,
    unsigned char ch,
    uint32_t fg,
    uint32_t bg,
    uint16_t style)
{
    if (surface == NULL || surface->cells == NULL ||
        x < 0 || y < 0 ||
        x >= surface->size.w || y >= surface->size.h) {
        return;
    }

    size_t index = (size_t)y * (size_t)surface->size.w + (size_t)x;
    surface->cells[index] = plugin_frame_cell(ch, fg, bg, style);
}

void plugin_frame_fill(
    DrawPluginSurface *surface,
    TgRecti rect,
    uint32_t fg,
    uint32_t bg,
    uint16_t style)
{
    if (surface == NULL || surface->cells == NULL ||
        surface->size.w <= 0 || surface->size.h <= 0 ||
        rect.w <= 0 || rect.h <= 0) {
        return;
    }

    int x_start = rect.x < 0 ? 0 : rect.x;
    int y_start = rect.y < 0 ? 0 : rect.y;
    int64_t raw_x_end = (int64_t)rect.x + rect.w;
    int64_t raw_y_end = (int64_t)rect.y + rect.h;
    int x_end = raw_x_end > surface->size.w
        ? surface->size.w
        : raw_x_end <= 0 ? 0 : (int)raw_x_end;
    int y_end = raw_y_end > surface->size.h
        ? surface->size.h
        : raw_y_end <= 0 ? 0 : (int)raw_y_end;

    for (int y = y_start; y < y_end; ++y) {
        for (int x = x_start; x < x_end; ++x) {
            plugin_frame_put(surface, x, y, ' ', fg, bg, style);
        }
    }
}

void plugin_frame_text(
    DrawPluginSurface *surface,
    int x,
    int y,
    const char *text,
    uint32_t fg,
    uint32_t bg,
    uint16_t style)
{
    if (surface == NULL || surface->cells == NULL ||
        surface->size.w <= 0 || surface->size.h <= 0 ||
        y < 0 || y >= surface->size.h || text == NULL) {
        return;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (i > (size_t)INT64_MAX) {
            break;
        }
        int64_t target_x = (int64_t)x + (int64_t)i;
        if (target_x >= surface->size.w) {
            break;
        }
        if (target_x < 0) {
            continue;
        }
        plugin_frame_put(
            surface,
            (int)target_x,
            y,
            (unsigned char)text[i],
            fg,
            bg,
            style);
    }
}

void plugin_frame_box(
    DrawPluginSurface *surface,
    TgRecti rect,
    const char *title)
{
    if (surface == NULL || surface->cells == NULL ||
        surface->size.w <= 0 || surface->size.h <= 0 ||
        rect.w <= 0 || rect.h <= 0) {
        return;
    }

    plugin_frame_fill(
        surface,
        rect,
        PLUGIN_FRAME_COLOR_TEXT,
        PLUGIN_FRAME_COLOR_PANEL_BG,
        TUI_STYLE_NONE);

    int64_t right = (int64_t)rect.x + rect.w - 1;
    int64_t bottom = (int64_t)rect.y + rect.h - 1;
    int x_start = rect.x < 0 ? 0 : rect.x;
    int x_end = right >= surface->size.w
        ? surface->size.w
        : right < 0 ? 0 : (int)right + 1;
    for (int x = x_start; x < x_end; ++x) {
        plugin_frame_put(
            surface, x, rect.y, '-',
            PLUGIN_FRAME_COLOR_BORDER,
            PLUGIN_FRAME_COLOR_PANEL_BG,
            TUI_STYLE_NONE);
        plugin_frame_put(
            surface, x,
            bottom > INT_MAX ? INT_MAX : (int)bottom,
            '-',
            PLUGIN_FRAME_COLOR_BORDER,
            PLUGIN_FRAME_COLOR_PANEL_BG,
            TUI_STYLE_NONE);
    }
    int y_start = rect.y < 0 ? 0 : rect.y;
    int y_end = bottom >= surface->size.h
        ? surface->size.h
        : bottom < 0 ? 0 : (int)bottom + 1;
    for (int y = y_start; y < y_end; ++y) {
        plugin_frame_put(
            surface, rect.x, y, '|',
            PLUGIN_FRAME_COLOR_BORDER,
            PLUGIN_FRAME_COLOR_PANEL_BG,
            TUI_STYLE_NONE);
        plugin_frame_put(
            surface,
            right > INT_MAX ? INT_MAX : (int)right,
            y, '|',
            PLUGIN_FRAME_COLOR_BORDER,
            PLUGIN_FRAME_COLOR_PANEL_BG,
            TUI_STYLE_NONE);
    }

    plugin_frame_put(
        surface, rect.x, rect.y, '+',
        PLUGIN_FRAME_COLOR_BORDER,
        PLUGIN_FRAME_COLOR_PANEL_BG,
        TUI_STYLE_NONE);
    plugin_frame_put(
        surface,
        right > INT_MAX ? INT_MAX : (int)right,
        rect.y, '+',
        PLUGIN_FRAME_COLOR_BORDER,
        PLUGIN_FRAME_COLOR_PANEL_BG,
        TUI_STYLE_NONE);
    plugin_frame_put(
        surface, rect.x,
        bottom > INT_MAX ? INT_MAX : (int)bottom,
        '+',
        PLUGIN_FRAME_COLOR_BORDER,
        PLUGIN_FRAME_COLOR_PANEL_BG,
        TUI_STYLE_NONE);
    plugin_frame_put(
        surface,
        right > INT_MAX ? INT_MAX : (int)right,
        bottom > INT_MAX ? INT_MAX : (int)bottom,
        '+',
        PLUGIN_FRAME_COLOR_BORDER,
        PLUGIN_FRAME_COLOR_PANEL_BG,
        TUI_STYLE_NONE);

    int64_t title_x = (int64_t)rect.x + 2;
    if (title != NULL && rect.w > 4 &&
        title_x >= INT_MIN && title_x <= INT_MAX) {
        plugin_frame_text(
            surface,
            (int)title_x,
            rect.y,
            title,
            PLUGIN_FRAME_COLOR_TEXT,
            PLUGIN_FRAME_COLOR_PANEL_BG,
            TUI_STYLE_BOLD);
    }
}
