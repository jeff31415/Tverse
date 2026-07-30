#ifndef DRAW_APP_CANVAS_STATE_H
#define DRAW_APP_CANVAS_STATE_H

#include "canvas.h"

/*
 * Reusable canvas editing state. Positions stored in samples and history are
 * world coordinates relative to the final-output center, never screen
 * coordinates.
 */
typedef struct CanvasStroke {
    bool active;
    TuiCell cell;
    CanvasSample *samples;
    size_t sample_count;
    size_t sample_capacity;
} CanvasStroke;

typedef struct CanvasState {
    CanvasDocument document;
    CanvasStroke pending_stroke;
} CanvasState;

typedef struct CanvasViewport {
    /* Destination rectangle, in screen/cell-buffer coordinates. */
    TgRecti rect;
    /* Screen coordinate at which world position (0, 0) is projected. */
    TgVec2i origin_screen;
} CanvasViewport;

typedef struct CanvasRenderStyle {
    uint32_t default_fg;
    uint32_t draft_bg;
    uint32_t output_bg;
} CanvasRenderStyle;

typedef struct CanvasRenderTarget {
    /* Dimensions and row width of the contiguous destination cell buffer. */
    TgSizei size;
    TuiCell *cells;
    CanvasViewport viewport;
    CanvasRenderStyle style;
} CanvasRenderTarget;

TgResult canvas_state_init(CanvasState *state, TgSizei output_size);
void canvas_state_destroy(CanvasState *state);
/* Clears history and a pending stroke while retaining output_size/storage. */
void canvas_state_reset(CanvasState *state);

/*
 * Beginning a stroke finalizes an existing one. Appending uses Bresenham
 * interpolation from the preceding sample and never adds adjacent duplicates.
 */
TgResult canvas_state_begin_stroke(
    CanvasState *state,
    TgVec2i position,
    TuiCell cell);
TgResult canvas_state_append_stroke(
    CanvasState *state,
    TgVec2i position);
TgResult canvas_state_finalize_stroke(CanvasState *state);
/* Cancels without committing; allocated sample capacity is retained. */
void canvas_state_cancel_stroke(CanvasState *state);
bool canvas_state_stroke_active(const CanvasState *state);

bool canvas_state_can_undo(const CanvasState *state);
bool canvas_state_can_redo(const CanvasState *state);
/* Undo/redo finalize a pending stroke before moving the history cursor. */
TgResult canvas_state_undo(CanvasState *state);
TgResult canvas_state_redo(CanvasState *state);

bool canvas_viewport_screen_to_world(
    const CanvasViewport *viewport,
    int screen_x,
    int screen_y,
    TgVec2i *out_position);
bool canvas_viewport_world_to_screen(
    const CanvasViewport *viewport,
    TgVec2i position,
    TgVec2i *out_screen);

/*
 * Rebuilds only the clipped viewport: region backgrounds first, applied
 * history second, and the pending stroke last.
 */
TgResult canvas_state_render(
    const CanvasState *state,
    const CanvasRenderTarget *target);

#endif
