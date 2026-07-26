#define _POSIX_C_SOURCE 200809L

#include "app.h"
#include "config.h"
#include "pages.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int64_t app_now_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (int64_t)now.tv_sec * 1000000000ll + (int64_t)now.tv_nsec;
}

static TuiCell app_default_cell(void)
{
    TuiCell cell;
    memset(&cell, 0, sizeof(cell));
    cell.ch[0] = ' ';
    cell.width = 1;
    cell.fg = TUI_COLOR_DEFAULT;
    cell.bg = TUI_COLOR_DEFAULT;
    cell.style = TUI_STYLE_NONE;
    return cell;
}

static bool app_frame_cell_count(TgSizei size, size_t *out_count)
{
    if (size.w <= 0 || size.h <= 0 || out_count == NULL) {
        return false;
    }

    size_t width = (size_t)size.w;
    size_t height = (size_t)size.h;
    if (width > SIZE_MAX / height) {
        return false;
    }

    *out_count = width * height;
    return true;
}

static TgResult app_config_read_positive_long(
    const Config *source,
    const char *directive,
    long maximum,
    long *destination)
{
    if (config_get_arg_idx(source, directive) == CONFIG_MAX_ARGS) {
        return TG_OK;
    }

    long value = 0;
    if (config_get_long_arg(source, directive, &value) != 0 ||
        value <= 0 ||
        value > maximum) {
        fprintf(stderr, "invalid draw_app config value: %s\n", directive);
        return TG_ERR_INVALID;
    }

    *destination = value;
    return TG_OK;
}

static void app_update_active_flags(App *app)
{
    for (size_t i = 0; i < app->page_count; ++i) {
        app->pages[i].active = i == app->active_page;
    }
}

static void app_draw_footer(App *app)
{
    TuiCell *screen = tui_get_buffer();
    if (screen == NULL || app->page_count == 0) {
        return;
    }

    int width = app->config.screen_size.w;
    int y = app->config.screen_size.h - app->config.footer_height;
    for (int x = 0; x < width; ++x) {
        size_t index = (size_t)y * (size_t)width + (size_t)x;
        screen[index] = app_default_cell();
        screen[index].style = TUI_STYLE_DIM;
    }

    for (size_t i = 0; i < app->page_count; ++i) {
        int start = (int)(i * (size_t)width / app->page_count);
        int end = (int)((i + 1u) * (size_t)width / app->page_count);
        int slot_width = end - start;
        if (slot_width <= 0) {
            continue;
        }

        for (int x = start; x < end; ++x) {
            size_t index = (size_t)y * (size_t)width + (size_t)x;
            screen[index].style = i == app->active_page
                ? (uint16_t)(TUI_STYLE_BOLD | TUI_STYLE_REVERSE)
                : TUI_STYLE_DIM;
        }

        char label[64];
        int label_length = snprintf(
            label,
            sizeof(label),
            "F%zu %s",
            i + 1u,
            app->pages[i].title);
        if (label_length <= 0) {
            continue;
        }

        size_t visible_length = (size_t)label_length;
        if (visible_length >= sizeof(label)) {
            visible_length = sizeof(label) - 1u;
        }
        if (visible_length > (size_t)slot_width) {
            visible_length = (size_t)slot_width;
        }

        int label_x = start + (slot_width - (int)visible_length) / 2;
        for (size_t j = 0; j < visible_length; ++j) {
            size_t index = (size_t)y * (size_t)width + (size_t)label_x + j;
            screen[index].ch[0] = label[j];
            screen[index].ch[1] = '\0';
        }
    }
}

void app_config_defaults(AppConfig *config)
{
    if (config == NULL) {
        return;
    }

    config->screen_size = (TgSizei){APP_DEFAULT_WIDTH, APP_DEFAULT_HEIGHT};
    config->target_fps = APP_DEFAULT_FPS;
    config->footer_height = APP_FOOTER_HEIGHT;
}

TgResult app_config_load(AppConfig *config, const char *path)
{
    if (config == NULL || path == NULL) {
        return TG_ERR_INVALID;
    }

    Config source;
    memset(&source, 0, sizeof(source));
    if (config_make(&source, path) != 0) {
        config_free(&source);
        return TG_ERR;
    }

    long width = config->screen_size.w;
    long height = config->screen_size.h;
    long fps = (long)config->target_fps;

    TgResult result = app_config_read_positive_long(
        &source, "tui_width", INT32_MAX, &width);
    if (tg_result_ok(result)) {
        result = app_config_read_positive_long(
            &source, "tui_height", INT32_MAX, &height);
    }
    if (tg_result_ok(result)) {
        result = app_config_read_positive_long(
            &source, "target_fps", UINT_MAX, &fps);
    }

    if (tg_result_ok(result) && height <= config->footer_height) {
        fputs("tui_height must leave at least one page row\n", stderr);
        result = TG_ERR_INVALID;
    }

    if (tg_result_ok(result)) {
        config->screen_size = (TgSizei){(int32_t)width, (int32_t)height};
        config->target_fps = (unsigned)fps;
    }

    config_free(&source);
    return result;
}

TgResult app_add_page(
    App *app,
    const char *title,
    TuiKey shortcut,
    PageUpdateFn update,
    void *userdata)
{
    if (app == NULL ||
        title == NULL ||
        update == NULL ||
        app->page_count >= APP_MAX_PAGES) {
        return TG_ERR_INVALID;
    }

    size_t cell_count = 0;
    if (!app_frame_cell_count(app->content_size, &cell_count)) {
        return TG_ERR_INVALID;
    }

    TuiCell *cells = calloc(cell_count, sizeof(*cells));
    if (cells == NULL) {
        return TG_ERR_NOMEM;
    }

    Page *page = &app->pages[app->page_count];
    *page = (Page){
        .title = title,
        .shortcut = shortcut,
        .active = app->page_count == app->active_page,
        .frame = {
            .size = app->content_size,
            .cells = cells,
        },
        .update = update,
        .userdata = userdata,
    };
    app_frame_clear(&page->frame);
    ++app->page_count;
    return TG_OK;
}

TgResult app_init(App *app, const AppConfig *config)
{
    if (app == NULL ||
        config == NULL ||
        config->screen_size.w <= 0 ||
        config->screen_size.h <= config->footer_height ||
        config->footer_height != APP_FOOTER_HEIGHT ||
        config->target_fps == 0) {
        return TG_ERR_INVALID;
    }

    memset(app, 0, sizeof(*app));
    app->config = *config;
    app->content_size = (TgSizei){
        config->screen_size.w,
        config->screen_size.h - config->footer_height,
    };
    app->running = true;

    TgResult result = tui_init(config->screen_size);
    if (tg_result_err(result)) {
        return result;
    }
    app->tui_ready = true;

    result = app_register_default_pages(app);
    if (tg_result_err(result)) {
        app_shutdown(app);
        return result;
    }

    app_update_active_flags(app);
    app->last_frame_ns = app_now_ns();
    return TG_OK;
}

void app_shutdown(App *app)
{
    if (app == NULL) {
        return;
    }

    for (size_t i = 0; i < app->page_count; ++i) {
        free(app->pages[i].frame.cells);
        app->pages[i].frame.cells = NULL;
    }
    app->page_count = 0;

    if (app->tui_ready) {
        tui_shutdown();
        app->tui_ready = false;
    }
    app->running = false;
}

bool app_should_close(const App *app)
{
    return app == NULL || !app->running;
}

void app_begin_frame(App *app)
{
    if (app == NULL) {
        return;
    }

    app->frame_start_ns = app_now_ns();
    if (app->last_frame_ns > 0 && app->frame_start_ns >= app->last_frame_ns) {
        app->delta_time =
            (double)(app->frame_start_ns - app->last_frame_ns) / 1000000000.0;
    } else {
        app->delta_time = 0.0;
    }
    app->last_frame_ns = app->frame_start_ns;

    if (tui_key_clicked(TUI_KEY_ESCAPE) || tui_char_clicked('q')) {
        app->running = false;
        return;
    }

    for (size_t i = 0; i < app->page_count; ++i) {
        if (tui_key_clicked(app->pages[i].shortcut)) {
            app->active_page = i;
            break;
        }
    }
    app_update_active_flags(app);
}

void app_update_pages(App *app)
{
    if (app == NULL) {
        return;
    }

    AppFrameContext context = {
        .frame_index = app->frame_index,
        .delta_time = app->delta_time,
    };
    for (size_t i = 0; i < app->page_count; ++i) {
        app->pages[i].update(&app->pages[i], &context);
    }
}

void app_compose(App *app)
{
    if (app == NULL || app->page_count == 0) {
        return;
    }

    tui_clear();
    TuiCell *screen = tui_get_buffer();
    Page *active = &app->pages[app->active_page];
    size_t cell_count = 0;
    if (screen != NULL &&
        app_frame_cell_count(active->frame.size, &cell_count)) {
        memcpy(screen, active->frame.cells, cell_count * sizeof(*screen));
    }

    app_draw_footer(app);
}

TgResult app_end_frame(App *app)
{
    if (app == NULL) {
        return TG_ERR_INVALID;
    }

    TgResult result = tui_present();
    if (tg_result_err(result)) {
        app->running = false;
        return result;
    }

    ++app->frame_index;
    int64_t frame_budget_ns = 1000000000ll / (int64_t)app->config.target_fps;
    int64_t elapsed_ns = app_now_ns() - app->frame_start_ns;
    int64_t remaining_ns = frame_budget_ns - elapsed_ns;
    if (remaining_ns > 0) {
        struct timespec delay = {
            .tv_sec = (time_t)(remaining_ns / 1000000000ll),
            .tv_nsec = (long)(remaining_ns % 1000000000ll),
        };
        while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
        }
    }
    return TG_OK;
}

void app_frame_clear(AppFrame *frame)
{
    if (frame == NULL || frame->cells == NULL) {
        return;
    }

    size_t cell_count = 0;
    if (!app_frame_cell_count(frame->size, &cell_count)) {
        return;
    }

    TuiCell cell = app_default_cell();
    for (size_t i = 0; i < cell_count; ++i) {
        frame->cells[i] = cell;
    }
}

void app_frame_draw_text(
    AppFrame *frame,
    int x,
    int y,
    const char *text,
    uint32_t fg,
    uint32_t bg,
    uint16_t style)
{
    if (frame == NULL ||
        frame->cells == NULL ||
        text == NULL ||
        y < 0 ||
        y >= frame->size.h) {
        return;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        int draw_x = x + (int)i;
        if (draw_x < 0) {
            continue;
        }
        if (draw_x >= frame->size.w) {
            break;
        }

        size_t index = (size_t)y * (size_t)frame->size.w + (size_t)draw_x;
        TuiCell *cell = &frame->cells[index];
        *cell = app_default_cell();
        cell->ch[0] = text[i];
        cell->ch[1] = '\0';
        cell->fg = fg;
        cell->bg = bg;
        cell->style = style;
    }
}
