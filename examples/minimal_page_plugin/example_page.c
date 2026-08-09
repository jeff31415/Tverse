#include "plugin.h"
#include "plugin_frame.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXAMPLE_TITLE_CAPACITY 64u
#define EXAMPLE_RAW_READ_CAPACITY 64u
#define EXAMPLE_COLOR_BG 0x10161du
#define EXAMPLE_COLOR_TEXT 0xd7e0e7u
#define EXAMPLE_COLOR_MUTED 0x82909cu
#define EXAMPLE_COLOR_ACCENT 0x4f8fcdu

typedef struct ExamplePage {
    DrawPluginHost host;
    TgSizei frame_size;
    TuiInputEvent last_event;
    uint64_t tick_count;
    uint64_t raw_byte_count;
    char title[EXAMPLE_TITLE_CAPACITY];
    bool active;
    bool has_event;
} ExamplePage;

static void example_copy_title(ExamplePage *page, TgBytes config)
{
    static const char fallback[] = "Example";
    const char *source = fallback;
    size_t length = sizeof(fallback) - 1u;

    if (config.data != NULL && config.len > 0) {
        source = (const char *)config.data;
        length = config.len;
    }
    if (length >= sizeof(page->title)) {
        length = sizeof(page->title) - 1u;
    }

    memcpy(page->title, source, length);
    page->title[length] = '\0';
}

static void example_read_raw_stdin(ExamplePage *page)
{
    if (page->host.stdin_read == NULL) {
        return;
    }

    unsigned char bytes[EXAMPLE_RAW_READ_CAPACITY];
    size_t count = page->host.stdin_read(
        page->host.userdata,
        bytes,
        sizeof(bytes));
    if (count > sizeof(bytes)) {
        count = sizeof(bytes);
    }
    if (UINT64_MAX - page->raw_byte_count < count) {
        page->raw_byte_count = UINT64_MAX;
    } else {
        page->raw_byte_count += (uint64_t)count;
    }
}

static TgResult example_render(
    const ExamplePage *page,
    DrawPluginSurface *surface)
{
    if (surface == NULL || surface->cells == NULL ||
        surface->size.w <= 0 || surface->size.h <= 0) {
        return TG_ERR_INVALID;
    }

    plugin_frame_fill(
        surface,
        (TgRecti){0, 0, surface->size.w, surface->size.h},
        EXAMPLE_COLOR_TEXT,
        EXAMPLE_COLOR_BG,
        TUI_STYLE_NONE);

    TgRecti box = {
        .x = surface->size.w > 4 ? 2 : 0,
        .y = surface->size.h > 4 ? 2 : 0,
        .w = surface->size.w > 4 ? surface->size.w - 4 : surface->size.w,
        .h = surface->size.h > 4 ? surface->size.h - 4 : surface->size.h,
    };
    plugin_frame_box(surface, box, page->title);

    int text_x = box.x + 2;
    int text_y = box.y + 2;
    plugin_frame_text(
        surface,
        text_x,
        text_y,
        "Minimal four-function page plugin",
        EXAMPLE_COLOR_TEXT,
        EXAMPLE_COLOR_BG,
        TUI_STYLE_BOLD);

    char status[128];
    (void)snprintf(
        status,
        sizeof(status),
        "active=%s  ticks=%" PRIu64 "  raw stdin bytes=%" PRIu64,
        page->active ? "yes" : "no",
        page->tick_count,
        page->raw_byte_count);
    plugin_frame_text(
        surface,
        text_x,
        text_y + 2,
        status,
        EXAMPLE_COLOR_MUTED,
        EXAMPLE_COLOR_BG,
        TUI_STYLE_NONE);

    const char *event_text = "last event: none";
    char event_status[96];
    if (page->has_event) {
        (void)snprintf(
            event_status,
            sizeof(event_status),
            "last event: type=%d key=%d ch=%u",
            (int)page->last_event.type,
            (int)page->last_event.key,
            (unsigned)page->last_event.ch);
        event_text = event_status;
    }
    plugin_frame_text(
        surface,
        text_x,
        text_y + 4,
        event_text,
        EXAMPLE_COLOR_ACCENT,
        EXAMPLE_COLOR_BG,
        TUI_STYLE_NONE);
    return TG_OK;
}

TgResult draw_plugin_entry(
    const DrawPluginOpenArgs *args,
    DrawPlugin **out_plugin)
{
    if (out_plugin == NULL) {
        return TG_ERR_INVALID;
    }
    *out_plugin = NULL;
    if (args == NULL) {
        return TG_ERR_INVALID;
    }
    if (args->abi_version != DRAW_PLUGIN_ABI_VERSION) {
        return TG_ERR_UNSUPPORTED;
    }
    if (args->frame_size.w <= 0 || args->frame_size.h <= 0 ||
        (args->config.len > 0 && args->config.data == NULL)) {
        return TG_ERR_INVALID;
    }

    ExamplePage *page = calloc(1, sizeof(*page));
    if (page == NULL) {
        return TG_ERR_NOMEM;
    }
    page->host = args->host;
    page->frame_size = args->frame_size;
    example_copy_title(page, args->config);
    *out_plugin = (DrawPlugin *)page;
    return TG_OK;
}

void draw_plugin_cleanup(DrawPlugin *plugin)
{
    free((ExamplePage *)plugin);
}

TgResult draw_plugin_write(
    DrawPlugin *plugin,
    DrawPluginWriteKind kind,
    const void *data)
{
    ExamplePage *page = (ExamplePage *)plugin;
    if (page == NULL) {
        return TG_ERR_INVALID;
    }

    switch (kind) {
    case DRAW_PLUGIN_WRITE_ENTER:
        if (data != NULL) {
            return TG_ERR_INVALID;
        }
        page->active = true;
        return TG_OK;
    case DRAW_PLUGIN_WRITE_LEAVE:
        if (data == NULL) {
            return TG_ERR_INVALID;
        }
        if (*(const DrawPluginLeaveReason *)data <
                DRAW_PLUGIN_LEAVE_SWITCH ||
            *(const DrawPluginLeaveReason *)data >
                DRAW_PLUGIN_LEAVE_SHUTDOWN) {
            return TG_ERR_INVALID;
        }
        page->active = false;
        return TG_OK;
    case DRAW_PLUGIN_WRITE_INPUT:
        if (data == NULL) {
            return TG_ERR_INVALID;
        }
        page->last_event = *(const TuiInputEvent *)data;
        page->has_event = true;
        return TG_OK;
    case DRAW_PLUGIN_WRITE_TICK: {
        if (data == NULL) {
            return TG_ERR_INVALID;
        }
        const DrawPluginFrameContext *context = data;
        if (context->frame_size.w <= 0 || context->frame_size.h <= 0) {
            return TG_ERR_INVALID;
        }
        page->frame_size = context->frame_size;
        ++page->tick_count;
        example_read_raw_stdin(page);
        return TG_OK;
    }
    default:
        return TG_ERR_INVALID;
    }
}

TgResult draw_plugin_read(
    DrawPlugin *plugin,
    DrawPluginReadKind kind,
    void *data)
{
    if (plugin == NULL || kind != DRAW_PLUGIN_READ_FRAME) {
        return TG_ERR_INVALID;
    }
    return example_render(
        (const ExamplePage *)plugin,
        (DrawPluginSurface *)data);
}
