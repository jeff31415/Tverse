#ifndef DRAW_APP_PLUGIN_FRAME_H
#define DRAW_APP_PLUGIN_FRAME_H

#include "plugin.h"

void plugin_frame_clear(DrawPluginSurface *surface);

void plugin_frame_put(
    DrawPluginSurface *surface,
    int x,
    int y,
    unsigned char ch,
    uint32_t fg,
    uint32_t bg,
    uint16_t style);

void plugin_frame_fill(
    DrawPluginSurface *surface,
    TgRecti rect,
    uint32_t fg,
    uint32_t bg,
    uint16_t style);

void plugin_frame_text(
    DrawPluginSurface *surface,
    int x,
    int y,
    const char *text,
    uint32_t fg,
    uint32_t bg,
    uint16_t style);

void plugin_frame_box(
    DrawPluginSurface *surface,
    TgRecti rect,
    const char *title);

#endif
