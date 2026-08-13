#include "plugin.h"
#include "plugin_frame.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLUGIN_TEMPLETE_TITLE_CAPACITY 64u
#define PLUGIN_TEMPLETE_RAW_CAPACITY 32u
#define PLUGIN_TEMPLETE_FG 0xd7e0e7u
#define PLUGIN_TEMPLETE_BG 0x10161du
#define PLUGIN_TEMPLETE_ACCENT 0x4f8fcdu

/*
 * This concrete type is private to the module.  The ABI exposes it only as
 * DrawPlugin *, so it may be replaced without changing plugin.h.
 */
typedef struct PluginTemplete {
    DrawPluginHost host;
    TgSizei frame_size;
    TuiInputEvent last_input;
    uint64_t frame_index;
    uint64_t raw_bytes_read;
    char title[PLUGIN_TEMPLETE_TITLE_CAPACITY];
    bool active;
    bool has_input;
} PluginTemplete;

static void plugin_templete_copy_title(
    PluginTemplete *plugin,
    TgBytes config)
{
    static const char fallback[] = "Plugin templete";
    const uint8_t *source = (const uint8_t *)fallback;
    size_t length = sizeof(fallback) - 1u;

    if (config.len > 0) {
        source = config.data;
        length = config.len;
    }
    if (length >= sizeof(plugin->title)) {
        length = sizeof(plugin->title) - 1u;
    }

    memcpy(plugin->title, source, length);
    plugin->title[length] = '\0';
}

static void plugin_templete_read_raw_stdin(PluginTemplete *plugin)
{
    if (plugin->host.stdin_read == NULL) {
        return;
    }

    unsigned char raw[PLUGIN_TEMPLETE_RAW_CAPACITY];
    size_t count = plugin->host.stdin_read(
        plugin->host.userdata,
        raw,
        sizeof(raw));

    /* Be defensive about a host callback that violates its capacity rule. */
    if (count > sizeof(raw)) {
        count = sizeof(raw);
    }
    if (UINT64_MAX - plugin->raw_bytes_read < (uint64_t)count) {
        plugin->raw_bytes_read = UINT64_MAX;
    } else {
        plugin->raw_bytes_read += (uint64_t)count;
    }
}

static TgResult plugin_templete_render(
    const PluginTemplete *plugin,
    DrawPluginSurface *surface)
{
    if (surface == NULL || surface->cells == NULL ||
        surface->size.w <= 0 || surface->size.h <= 0) {
        return TG_ERR_INVALID;
    }

    plugin_frame_fill(
        surface,
        (TgRecti){0, 0, surface->size.w, surface->size.h},
        PLUGIN_TEMPLETE_FG,
        PLUGIN_TEMPLETE_BG,
        TUI_STYLE_NONE);
    plugin_frame_text(
        surface,
        2,
        1,
        plugin->title,
        PLUGIN_TEMPLETE_ACCENT,
        PLUGIN_TEMPLETE_BG,
        TUI_STYLE_BOLD);

    char status[160];
    (void)snprintf(
        status,
        sizeof(status),
        "active=%s frame=%" PRIu64 " raw-bytes=%" PRIu64,
        plugin->active ? "yes" : "no",
        plugin->frame_index,
        plugin->raw_bytes_read);
    plugin_frame_text(
        surface,
        2,
        3,
        status,
        PLUGIN_TEMPLETE_FG,
        PLUGIN_TEMPLETE_BG,
        TUI_STYLE_NONE);

    const char *input_text = "last decoded input: none";
    char input_status[128];
    if (plugin->has_input) {
        (void)snprintf(
            input_status,
            sizeof(input_status),
            "last decoded input: type=%d key=%d ch=%u",
            (int)plugin->last_input.type,
            (int)plugin->last_input.key,
            (unsigned)plugin->last_input.ch);
        input_text = input_status;
    }
    plugin_frame_text(
        surface,
        2,
        5,
        input_text,
        PLUGIN_TEMPLETE_FG,
        PLUGIN_TEMPLETE_BG,
        TUI_STYLE_NONE);
    return TG_OK;
}

/* Required ABI function: allocate one independent plugin instance. */
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
    if (args->frame_size.w <= 0 ||
        args->frame_size.h <= 0 ||
        (args->config.len > 0 && args->config.data == NULL)) {
        return TG_ERR_INVALID;
    }

    PluginTemplete *plugin = calloc(1, sizeof(*plugin));
    if (plugin == NULL) {
        return TG_ERR_NOMEM;
    }

    /* args/config are borrowed; copy everything needed after this call. */
    plugin->host = args->host;
    plugin->frame_size = args->frame_size;
    plugin_templete_copy_title(plugin, args->config);

    *out_plugin = (DrawPlugin *)plugin;
    return TG_OK;
}

/* Required ABI function: release all state before the host calls dlclose(). */
void draw_plugin_cleanup(DrawPlugin *plugin)
{
    free((PluginTemplete *)plugin);
}

/* Required ABI function: synchronously receive host-to-plugin operations. */
TgResult draw_plugin_write(
    DrawPlugin *plugin,
    DrawPluginWriteKind kind,
    const void *data)
{
    PluginTemplete *state = (PluginTemplete *)plugin;
    if (state == NULL) {
        return TG_ERR_INVALID;
    }

    switch (kind) {
    case DRAW_PLUGIN_WRITE_ENTER:
        if (data != NULL) {
            return TG_ERR_INVALID;
        }
        state->active = true;
        return TG_OK;

    case DRAW_PLUGIN_WRITE_LEAVE: {
        if (data == NULL) {
            return TG_ERR_INVALID;
        }
        DrawPluginLeaveReason reason =
            *(const DrawPluginLeaveReason *)data;
        if (reason < DRAW_PLUGIN_LEAVE_SWITCH ||
            reason > DRAW_PLUGIN_LEAVE_SHUTDOWN) {
            return TG_ERR_INVALID;
        }
        state->active = false;
        return TG_OK;
    }

    case DRAW_PLUGIN_WRITE_INPUT:
        if (data == NULL) {
            return TG_ERR_INVALID;
        }
        /* The pointer is borrowed, so retain a value copy rather than it. */
        state->last_input = *(const TuiInputEvent *)data;
        state->has_input = true;
        return TG_OK;

    case DRAW_PLUGIN_WRITE_TICK: {
        if (data == NULL) {
            return TG_ERR_INVALID;
        }
        const DrawPluginFrameContext *context =
            (const DrawPluginFrameContext *)data;
        if (context->frame_size.w <= 0 || context->frame_size.h <= 0 ||
            !(context->delta_time >= 0.0)) {
            return TG_ERR_INVALID;
        }
        state->frame_index = context->frame_index;
        state->frame_size = context->frame_size;

        /* Optional pull-style raw input; remove this if it is not needed. */
        plugin_templete_read_raw_stdin(state);
        return TG_OK;
    }

    default:
        return TG_ERR_INVALID;
    }
}

/* Required ABI function: synchronously produce plugin-to-host output. */
TgResult draw_plugin_read(
    DrawPlugin *plugin,
    DrawPluginReadKind kind,
    void *data)
{
    if (plugin == NULL || kind != DRAW_PLUGIN_READ_FRAME) {
        return TG_ERR_INVALID;
    }

    /* data/cells are borrowed mutable storage and must not be retained. */
    return plugin_templete_render(
        (const PluginTemplete *)plugin,
        (DrawPluginSurface *)data);
}
