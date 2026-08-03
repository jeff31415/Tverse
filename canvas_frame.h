#ifndef DRAW_APP_CANVAS_FRAME_H
#define DRAW_APP_CANVAS_FRAME_H

#include "app.h"

/* Bounds-checked drawing primitives shared by page implementations. */
void canvas_frame_put(
    AppFrame *frame,
    int x,
    int y,
    unsigned char ch,
    uint32_t fg,
    uint32_t bg,
    uint16_t style);

void canvas_frame_fill(
    AppFrame *frame,
    TgRecti rect,
    uint32_t fg,
    uint32_t bg,
    uint16_t style);

void canvas_frame_text(
    AppFrame *frame,
    int x,
    int y,
    const char *text,
    uint32_t fg,
    uint32_t bg,
    uint16_t style);

void canvas_frame_box(
    AppFrame *frame,
    TgRecti rect,
    const char *title);

#endif
