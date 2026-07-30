#include "canvas_state.h"

#include <stdlib.h>
#include <string.h>

typedef struct CanvasStateReplayContext {
    const CanvasState *state;
    const CanvasRenderTarget *target;
} CanvasStateReplayContext;

static bool canvas_state_rect_contains(TgRecti rect, int x, int y)
{
    return rect.w > 0 &&
           rect.h > 0 &&
           x >= rect.x &&
           y >= rect.y &&
           (int64_t)x < (int64_t)rect.x + rect.w &&
           (int64_t)y < (int64_t)rect.y + rect.h;
}

static TuiCell canvas_state_blank_cell(
    uint32_t fg,
    uint32_t bg)
{
    TuiCell cell;
    memset(&cell, 0, sizeof(cell));
    cell.ch[0] = ' ';
    cell.width = 1;
    cell.fg = fg;
    cell.bg = bg;
    cell.style = TUI_STYLE_NONE;
    return cell;
}

static TgResult canvas_stroke_reserve(
    CanvasStroke *stroke,
    size_t required)
{
    if (required <= stroke->sample_capacity) {
        return TG_OK;
    }

    size_t capacity =
        stroke->sample_capacity == 0 ? 32u : stroke->sample_capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            return TG_ERR_NOMEM;
        }
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(*stroke->samples)) {
        return TG_ERR_NOMEM;
    }

    CanvasSample *samples =
        realloc(stroke->samples, capacity * sizeof(*samples));
    if (samples == NULL) {
        return TG_ERR_NOMEM;
    }

    stroke->samples = samples;
    stroke->sample_capacity = capacity;
    return TG_OK;
}

static TgResult canvas_stroke_append_raw(
    CanvasStroke *stroke,
    TgVec2i position)
{
    if (stroke->sample_count > 0) {
        const CanvasSample *last =
            &stroke->samples[stroke->sample_count - 1u];
        if (last->position.x == position.x &&
            last->position.y == position.y) {
            return TG_OK;
        }
    }

    TgResult result =
        canvas_stroke_reserve(stroke, stroke->sample_count + 1u);
    if (tg_result_err(result)) {
        return result;
    }

    stroke->samples[stroke->sample_count++] = (CanvasSample){
        .position = position,
        .cell = stroke->cell,
    };
    return TG_OK;
}

static void canvas_state_render_sample(
    const CanvasSample *sample,
    void *userdata)
{
    CanvasStateReplayContext *context = userdata;
    const CanvasRenderTarget *target = context->target;
    TgVec2i screen;
    if (!canvas_viewport_world_to_screen(
            &target->viewport,
            sample->position,
            &screen) ||
        screen.x < 0 ||
        screen.y < 0 ||
        screen.x >= target->size.w ||
        screen.y >= target->size.h) {
        return;
    }

    uint32_t background = canvas_document_contains_output(
        &context->state->document,
        sample->position)
        ? target->style.output_bg
        : target->style.draft_bg;

    TuiCell cell = sample->cell;
    if (cell.ch[0] == '\0') {
        cell.ch[0] = ' ';
    }
    if (cell.width == 0) {
        cell.width = 1;
    }
    if (cell.fg == TUI_COLOR_DEFAULT) {
        cell.fg = target->style.default_fg;
    }
    if (cell.bg == TUI_COLOR_DEFAULT) {
        cell.bg = background;
    }

    size_t index =
        (size_t)screen.y * (size_t)target->size.w +
        (size_t)screen.x;
    target->cells[index] = cell;
}

TgResult canvas_state_init(CanvasState *state, TgSizei output_size)
{
    if (state == NULL) {
        return TG_ERR_INVALID;
    }

    memset(state, 0, sizeof(*state));
    return canvas_document_init(&state->document, output_size);
}

void canvas_state_destroy(CanvasState *state)
{
    if (state == NULL) {
        return;
    }

    free(state->pending_stroke.samples);
    state->pending_stroke.samples = NULL;
    canvas_document_destroy(&state->document);
    memset(state, 0, sizeof(*state));
}

void canvas_state_reset(CanvasState *state)
{
    if (state == NULL) {
        return;
    }

    canvas_state_cancel_stroke(state);
    canvas_document_reset(&state->document);
}

TgResult canvas_state_begin_stroke(
    CanvasState *state,
    TgVec2i position,
    TuiCell cell)
{
    if (state == NULL) {
        return TG_ERR_INVALID;
    }

    TgResult result = canvas_state_finalize_stroke(state);
    if (tg_result_err(result)) {
        return result;
    }

    CanvasStroke *stroke = &state->pending_stroke;
    stroke->active = true;
    stroke->cell = cell;
    stroke->sample_count = 0;
    result = canvas_stroke_append_raw(stroke, position);
    if (tg_result_err(result)) {
        canvas_state_cancel_stroke(state);
    }
    return result;
}

TgResult canvas_state_append_stroke(
    CanvasState *state,
    TgVec2i position)
{
    if (state == NULL || !state->pending_stroke.active) {
        return TG_ERR_INVALID;
    }

    CanvasStroke *stroke = &state->pending_stroke;
    if (stroke->sample_count == 0) {
        return canvas_stroke_append_raw(stroke, position);
    }

    TgVec2i last = stroke->samples[stroke->sample_count - 1u].position;
    int64_t x = last.x;
    int64_t y = last.y;
    int64_t dx =
        position.x > x ? (int64_t)position.x - x : x - position.x;
    int64_t sx = x < position.x ? 1 : -1;
    int64_t dy_abs =
        position.y > y ? (int64_t)position.y - y : y - position.y;
    int64_t dy = -dy_abs;
    int64_t sy = y < position.y ? 1 : -1;
    int64_t error = dx + dy;
    uint64_t additional =
        (uint64_t)(dx > dy_abs ? dx : dy_abs);
    if (additional > SIZE_MAX - stroke->sample_count) {
        return TG_ERR_NOMEM;
    }
    TgResult result = canvas_stroke_reserve(
        stroke,
        stroke->sample_count + (size_t)additional);
    if (tg_result_err(result)) {
        return result;
    }

    while (x != position.x || y != position.y) {
        int64_t twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y += sy;
        }

        result = canvas_stroke_append_raw(
            stroke,
            (TgVec2i){(int32_t)x, (int32_t)y});
        if (tg_result_err(result)) {
            return result;
        }
    }
    return TG_OK;
}

TgResult canvas_state_finalize_stroke(CanvasState *state)
{
    if (state == NULL) {
        return TG_ERR_INVALID;
    }
    if (!state->pending_stroke.active) {
        return TG_OK;
    }

    TgResult result = canvas_document_commit_draw(
        &state->document,
        state->pending_stroke.samples,
        state->pending_stroke.sample_count);
    if (tg_result_err(result)) {
        return result;
    }

    state->pending_stroke.active = false;
    state->pending_stroke.sample_count = 0;
    return TG_OK;
}

void canvas_state_cancel_stroke(CanvasState *state)
{
    if (state == NULL) {
        return;
    }
    state->pending_stroke.active = false;
    state->pending_stroke.sample_count = 0;
}

bool canvas_state_stroke_active(const CanvasState *state)
{
    return state != NULL && state->pending_stroke.active;
}

bool canvas_state_can_undo(const CanvasState *state)
{
    return state != NULL &&
           canvas_document_can_undo(&state->document);
}

bool canvas_state_can_redo(const CanvasState *state)
{
    return state != NULL &&
           canvas_document_can_redo(&state->document);
}

TgResult canvas_state_undo(CanvasState *state)
{
    TgResult result = canvas_state_finalize_stroke(state);
    if (tg_result_err(result)) {
        return result;
    }
    (void)canvas_document_undo(&state->document);
    return TG_OK;
}

TgResult canvas_state_redo(CanvasState *state)
{
    TgResult result = canvas_state_finalize_stroke(state);
    if (tg_result_err(result)) {
        return result;
    }
    (void)canvas_document_redo(&state->document);
    return TG_OK;
}

bool canvas_viewport_screen_to_world(
    const CanvasViewport *viewport,
    int screen_x,
    int screen_y,
    TgVec2i *out_position)
{
    if (viewport == NULL ||
        out_position == NULL ||
        !canvas_state_rect_contains(
            viewport->rect,
            screen_x,
            screen_y)) {
        return false;
    }

    int64_t world_x =
        (int64_t)screen_x - viewport->origin_screen.x;
    int64_t world_y =
        (int64_t)screen_y - viewport->origin_screen.y;
    if (world_x < INT32_MIN ||
        world_x > INT32_MAX ||
        world_y < INT32_MIN ||
        world_y > INT32_MAX) {
        return false;
    }

    out_position->x = (int32_t)world_x;
    out_position->y = (int32_t)world_y;
    return true;
}

bool canvas_viewport_world_to_screen(
    const CanvasViewport *viewport,
    TgVec2i position,
    TgVec2i *out_screen)
{
    if (viewport == NULL || out_screen == NULL) {
        return false;
    }

    int64_t screen_x =
        (int64_t)viewport->origin_screen.x + position.x;
    int64_t screen_y =
        (int64_t)viewport->origin_screen.y + position.y;
    if (screen_x < INT32_MIN ||
        screen_x > INT32_MAX ||
        screen_y < INT32_MIN ||
        screen_y > INT32_MAX ||
        !canvas_state_rect_contains(
            viewport->rect,
            (int)screen_x,
            (int)screen_y)) {
        return false;
    }

    out_screen->x = (int32_t)screen_x;
    out_screen->y = (int32_t)screen_y;
    return true;
}

TgResult canvas_state_render(
    const CanvasState *state,
    const CanvasRenderTarget *target)
{
    if (state == NULL ||
        target == NULL ||
        target->cells == NULL ||
        target->size.w <= 0 ||
        target->size.h <= 0 ||
        target->viewport.rect.w < 0 ||
        target->viewport.rect.h < 0) {
        return TG_ERR_INVALID;
    }

    int x_start = target->viewport.rect.x < 0
        ? 0
        : target->viewport.rect.x;
    int y_start = target->viewport.rect.y < 0
        ? 0
        : target->viewport.rect.y;
    int64_t raw_x_end =
        (int64_t)target->viewport.rect.x +
        target->viewport.rect.w;
    int64_t raw_y_end =
        (int64_t)target->viewport.rect.y +
        target->viewport.rect.h;
    int x_end = raw_x_end <= 0
        ? 0
        : raw_x_end >= target->size.w
            ? target->size.w
            : (int)raw_x_end;
    int y_end = raw_y_end <= 0
        ? 0
        : raw_y_end >= target->size.h
            ? target->size.h
            : (int)raw_y_end;

    for (int y = y_start; y < y_end; ++y) {
        for (int x = x_start; x < x_end; ++x) {
            TgVec2i position;
            bool has_world = canvas_viewport_screen_to_world(
                &target->viewport,
                x,
                y,
                &position);
            uint32_t background =
                has_world && canvas_document_contains_output(
                    &state->document,
                    position)
                ? target->style.output_bg
                : target->style.draft_bg;
            size_t index =
                (size_t)y * (size_t)target->size.w + (size_t)x;
            target->cells[index] = canvas_state_blank_cell(
                target->style.default_fg,
                background);
        }
    }

    CanvasStateReplayContext context = {
        .state = state,
        .target = target,
    };
    canvas_document_replay(
        &state->document,
        canvas_state_render_sample,
        &context);
    for (size_t i = 0;
         i < state->pending_stroke.sample_count;
         ++i) {
        canvas_state_render_sample(
            &state->pending_stroke.samples[i],
            &context);
    }
    return TG_OK;
}
