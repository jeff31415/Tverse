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

/*
 * Opaque, plugin-owned instance state.  A plugin defines its own concrete
 * structure and returns it as DrawPlugin *.  The host never dereferences it.
 */
typedef struct DrawPlugin DrawPlugin;

/*
 * Optional pull-style access to the host's raw stdin byte ring.
 *
 * The callback copies at most capacity bytes into destination, consumes the
 * copied bytes, and returns the number copied.  destination is writable only
 * for the duration of the call.  A zero return means "no bytes available".
 */
typedef size_t (*DrawPluginStdinReadFn)(
    void *userdata,
    void *destination,
    size_t capacity);

/* Host services copied by a plugin that needs them after entry returns. */
typedef struct DrawPluginHost {
    /* Opaque first argument passed unchanged to every host callback. */
    void *userdata;

    /* Optional raw-input callback; may be NULL. */
    DrawPluginStdinReadFn stdin_read;
} DrawPluginHost;

/* Borrowed construction arguments supplied to draw_plugin_entry(). */
typedef struct DrawPluginOpenArgs {
    /* Must equal DRAW_PLUGIN_ABI_VERSION. */
    unsigned abi_version;

    /* Initial drawable page size, excluding host-owned UI such as a footer. */
    TgSizei frame_size;

    /* Plugin-specific borrowed bytes; not necessarily text or NUL-terminated. */
    TgBytes config;

    /* Host callback table.  Copy the value before entry returns if retained. */
    DrawPluginHost host;
} DrawPluginOpenArgs;

/* Borrowed payload for DRAW_PLUGIN_WRITE_TICK. */
typedef struct DrawPluginFrameContext {
    /* Number of frames the host has successfully presented. */
    uint64_t frame_index;

    /* Monotonic elapsed time since the preceding frame, in seconds. */
    double delta_time;

    /* Current drawable page size; a change communicates a resize. */
    TgSizei frame_size;
} DrawPluginFrameContext;

/* Host-owned mutable output payload for DRAW_PLUGIN_READ_FRAME. */
typedef struct DrawPluginSurface {
    /* Dimensions of cells; both components are positive. */
    TgSizei size;

    /* Borrowed row-major array containing size.w * size.h TuiCell values. */
    TuiCell *cells;
} DrawPluginSurface;

/* Why an active instance is receiving DRAW_PLUGIN_WRITE_LEAVE. */
typedef enum DrawPluginLeaveReason {
    DRAW_PLUGIN_LEAVE_SWITCH = 0,
    DRAW_PLUGIN_LEAVE_RELOAD,
    DRAW_PLUGIN_LEAVE_SHUTDOWN
} DrawPluginLeaveReason;

/* Host-to-plugin operations accepted by draw_plugin_write(). */
typedef enum DrawPluginWriteKind {
    /* data must be NULL. */
    DRAW_PLUGIN_WRITE_ENTER = 0,
    /* data points to a borrowed const DrawPluginLeaveReason. */
    DRAW_PLUGIN_WRITE_LEAVE,
    /* data points to a borrowed const TuiInputEvent. */
    DRAW_PLUGIN_WRITE_INPUT,
    /* data points to a borrowed const DrawPluginFrameContext. */
    DRAW_PLUGIN_WRITE_TICK
} DrawPluginWriteKind;

/* Plugin-to-host output requests accepted by draw_plugin_read(). */
typedef enum DrawPluginReadKind {
    /* data points to a borrowed mutable DrawPluginSurface. */
    DRAW_PLUGIN_READ_FRAME = 0
} DrawPluginReadKind;

/* Construct one independent instance; on failure leave *out_plugin NULL. */
DRAW_PLUGIN_EXPORT TgResult draw_plugin_entry(
    const DrawPluginOpenArgs *args,
    DrawPlugin **out_plugin);

/* Destroy all instance-owned state.  The host calls this before dlclose(). */
DRAW_PLUGIN_EXPORT void draw_plugin_cleanup(DrawPlugin *plugin);

/*
 * Synchronously deliver one tagged, borrowed input/lifecycle payload.
 * The kind determines the exact type and nullability of data.
 */
DRAW_PLUGIN_EXPORT TgResult draw_plugin_write(
    DrawPlugin *plugin,
    DrawPluginWriteKind kind,
    const void *data);

/*
 * Synchronously request one tagged output operation in host-owned storage.
 * The kind determines the exact mutable type carried by data.
 */
DRAW_PLUGIN_EXPORT TgResult draw_plugin_read(
    DrawPlugin *plugin,
    DrawPluginReadKind kind,
    void *data);

#if defined(__cplusplus)
}
#endif

#endif
