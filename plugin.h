#ifndef DRAW_APP_PLUGIN_H
#define DRAW_APP_PLUGIN_H

#include "tui.h"

#include <stddef.h>
#include <stdint.h>

#define DRAW_PLUGIN_ABI_VERSION 1u

#if defined(__GNUC__) || defined(__clang__)
#define DRAW_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define DRAW_PLUGIN_EXPORT
#endif

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct DrawPlugin DrawPlugin;

typedef size_t (*DrawPluginStdinReadFn)(
    void *userdata,
    void *destination,
    size_t capacity);

typedef struct DrawPluginHost {
    void *userdata;
    DrawPluginStdinReadFn stdin_read;
} DrawPluginHost;

typedef struct DrawPluginOpenArgs {
    unsigned abi_version;
    TgSizei frame_size;
    TgBytes config;
    DrawPluginHost host;
} DrawPluginOpenArgs;

typedef struct DrawPluginFrameContext {
    uint64_t frame_index;
    double delta_time;
    TgSizei frame_size;
} DrawPluginFrameContext;

typedef struct DrawPluginSurface {
    TgSizei size;
    TuiCell *cells;
} DrawPluginSurface;

typedef enum DrawPluginLeaveReason {
    DRAW_PLUGIN_LEAVE_SWITCH = 0,
    DRAW_PLUGIN_LEAVE_RELOAD,
    DRAW_PLUGIN_LEAVE_SHUTDOWN
} DrawPluginLeaveReason;

typedef enum DrawPluginWriteKind {
    DRAW_PLUGIN_WRITE_ENTER = 0,
    DRAW_PLUGIN_WRITE_LEAVE,
    DRAW_PLUGIN_WRITE_INPUT,
    DRAW_PLUGIN_WRITE_TICK
} DrawPluginWriteKind;

typedef enum DrawPluginReadKind {
    DRAW_PLUGIN_READ_FRAME = 0
} DrawPluginReadKind;

DRAW_PLUGIN_EXPORT TgResult draw_plugin_entry(
    const DrawPluginOpenArgs *args,
    DrawPlugin **out_plugin);

DRAW_PLUGIN_EXPORT void draw_plugin_cleanup(DrawPlugin *plugin);

DRAW_PLUGIN_EXPORT TgResult draw_plugin_write(
    DrawPlugin *plugin,
    DrawPluginWriteKind kind,
    const void *data);

DRAW_PLUGIN_EXPORT TgResult draw_plugin_read(
    DrawPlugin *plugin,
    DrawPluginReadKind kind,
    void *data);

#if defined(__cplusplus)
}
#endif

#endif
