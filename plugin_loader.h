#ifndef DRAW_APP_PLUGIN_LOADER_H
#define DRAW_APP_PLUGIN_LOADER_H

#include "plugin.h"

#include <stdbool.h>
#include <stdint.h>

typedef TgResult (*DrawPluginEntryFn)(
    const DrawPluginOpenArgs *args,
    DrawPlugin **out_plugin);
typedef void (*DrawPluginCleanupFn)(DrawPlugin *plugin);
typedef TgResult (*DrawPluginWriteFn)(
    DrawPlugin *plugin,
    DrawPluginWriteKind kind,
    const void *data);
typedef TgResult (*DrawPluginReadFn)(
    DrawPlugin *plugin,
    DrawPluginReadKind kind,
    void *data);

typedef struct DrawPluginFunctions {
    DrawPluginEntryFn entry;
    DrawPluginCleanupFn cleanup;
    DrawPluginWriteFn write;
    DrawPluginReadFn read;
} DrawPluginFunctions;

typedef struct DrawPluginModule {
    void *handle;
    DrawPlugin *instance;
    DrawPluginFunctions functions;
    char *generation_path;
} DrawPluginModule;

typedef struct DrawPluginFileStamp {
    int64_t modified_ns;
    uint64_t size;
} DrawPluginFileStamp;

TgResult draw_plugin_file_stamp(
    const char *path,
    DrawPluginFileStamp *out_stamp);

bool draw_plugin_file_stamp_equal(
    DrawPluginFileStamp left,
    DrawPluginFileStamp right);

TgResult draw_plugin_module_open(
    const char *source_path,
    const DrawPluginOpenArgs *args,
    DrawPluginModule *out_module);

void draw_plugin_module_close(DrawPluginModule *module);

#endif
