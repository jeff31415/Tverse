#ifndef DRAW_APP_PLUGIN_CANVAS_INTERNAL_H
#define DRAW_APP_PLUGIN_CANVAS_INTERNAL_H

#include "canvas.h"

struct CanvasOperation {
    CanvasOperationType type;
    CanvasOperation *prev;
    CanvasOperation *next;
    CanvasSample *samples;
    size_t sample_count;
};

#endif
