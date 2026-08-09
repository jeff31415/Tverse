#define _POSIX_C_SOURCE 200809L

#include "app.h"
#include "config.h"
#include "pages.h"
#include "plugin_frame.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define APP_PLUGIN_STABLE_OBSERVATIONS 2u

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

static size_t app_plugin_stdin_read(
    void *userdata,
    void *destination,
    size_t capacity)
{
    (void)userdata;
    return tui_stdin_read(destination, capacity);
}

static DrawPluginOpenArgs app_plugin_open_args(
    App *app,
    const Page *page)
{
    return (DrawPluginOpenArgs){
        .abi_version = DRAW_PLUGIN_ABI_VERSION,
        .frame_size = app->content_size,
        .config = page->config,
        .host = {
            .userdata = app,
            .stdin_read = app_plugin_stdin_read,
        },
    };
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

static TgResult app_set_active_page(
    App *app,
    size_t page_index,
    DrawPluginLeaveReason leave_reason)
{
    if (app == NULL || page_index >= app->page_count) {
        return TG_ERR_INVALID;
    }
    if (page_index == app->active_page &&
        app->pages[page_index].entered) {
        return TG_OK;
    }

    if (app->active_page < app->page_count) {
        Page *old_page = &app->pages[app->active_page];
        if (old_page->entered) {
            TgResult result = old_page->module.functions.write(
                old_page->module.instance,
                DRAW_PLUGIN_WRITE_LEAVE,
                &leave_reason);
            if (tg_result_err(result)) {
                return result;
            }
        }
        old_page->entered = false;
    }

    app->active_page = page_index;
    app_update_active_flags(app);

    Page *new_page = &app->pages[page_index];
    TgResult result = new_page->module.functions.write(
        new_page->module.instance,
        DRAW_PLUGIN_WRITE_ENTER,
        NULL);
    if (tg_result_err(result)) {
        return result;
    }
    new_page->entered = true;
    return TG_OK;
}

static bool app_event_is_control_char(
    const TuiInputEvent *event,
    unsigned char ch)
{
    return event != NULL &&
           event->type == TUI_INPUT_KEY &&
           (event->modifiers & TUI_MOD_CONTROL) != 0 &&
           event->ch == ch;
}

static bool app_event_is_plain_key(
    const TuiInputEvent *event,
    TuiKey key)
{
    return event != NULL &&
           event->type == TUI_INPUT_KEY &&
           event->modifiers == TUI_MOD_NONE &&
           event->key == key;
}

static TgResult app_reload_page(
    App *app,
    Page *page,
    DrawPluginFileStamp source_stamp)
{
    if (app == NULL || page == NULL) {
        return TG_ERR_INVALID;
    }

    DrawPluginOpenArgs args = app_plugin_open_args(app, page);
    DrawPluginModule candidate;
    TgResult result = draw_plugin_module_open(
        page->module_path,
        &args,
        &candidate);
    if (tg_result_err(result)) {
        return result;
    }

    if (page->entered) {
        result = candidate.functions.write(
            candidate.instance,
            DRAW_PLUGIN_WRITE_ENTER,
            NULL);
        if (tg_result_err(result)) {
            draw_plugin_module_close(&candidate);
            return result;
        }

        DrawPluginLeaveReason reason = DRAW_PLUGIN_LEAVE_RELOAD;
        TgResult leave_result = page->module.functions.write(
            page->module.instance,
            DRAW_PLUGIN_WRITE_LEAVE,
            &reason);
        if (tg_result_err(leave_result)) {
            fprintf(stderr,
                "draw_app: plugin leave failed during reload: %s (%d)\n",
                page->title,
                leave_result);
        }
    }

    DrawPluginModule previous = page->module;
    page->module = candidate;
    page->loaded_stamp = source_stamp;
    ++page->generation;
    page->pending_valid = false;
    page->attempted_valid = false;
    page->pending_observations = 0;
    plugin_frame_clear(&page->frame);
    draw_plugin_module_close(&previous);
    return TG_OK;
}

static void app_poll_page_reload(App *app, Page *page)
{
    DrawPluginFileStamp current;
    if (tg_result_err(draw_plugin_file_stamp(page->module_path, &current))) {
        return;
    }

    if (draw_plugin_file_stamp_equal(current, page->loaded_stamp)) {
        page->pending_valid = false;
        page->attempted_valid = false;
        page->pending_observations = 0;
        return;
    }

    if (page->attempted_valid &&
        draw_plugin_file_stamp_equal(current, page->attempted_stamp)) {
        return;
    }

    if (!page->pending_valid ||
        !draw_plugin_file_stamp_equal(current, page->pending_stamp)) {
        page->pending_stamp = current;
        page->pending_valid = true;
        page->pending_observations = 1;
        return;
    }

    if (page->pending_observations < APP_PLUGIN_STABLE_OBSERVATIONS) {
        ++page->pending_observations;
    }
    if (page->pending_observations < APP_PLUGIN_STABLE_OBSERVATIONS) {
        return;
    }

    page->attempted_stamp = current;
    page->attempted_valid = true;
    TgResult result = app_reload_page(app, page, current);
    if (tg_result_err(result)) {
        fprintf(stderr,
            "draw_app: plugin reload failed, keeping current page: %s (%d)\n",
            page->title,
            result);
    }
}

static void app_poll_plugin_reloads(App *app)
{
    for (size_t i = 0; i < app->page_count; ++i) {
        app_poll_page_reload(app, &app->pages[i]);
    }
}

static void app_force_reload_active_page(App *app)
{
    if (app == NULL || app->active_page >= app->page_count) {
        return;
    }

    Page *page = &app->pages[app->active_page];
    DrawPluginFileStamp current;
    TgResult result = draw_plugin_file_stamp(page->module_path, &current);
    if (tg_result_ok(result)) {
        result = app_reload_page(app, page, current);
    }
    if (tg_result_err(result)) {
        fprintf(stderr,
            "draw_app: forced plugin reload failed: %s (%d)\n",
            page->title,
            result);
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
    config->canvas_output_size = (TgSizei){
        APP_DEFAULT_CANVAS_WIDTH,
        APP_DEFAULT_CANVAS_HEIGHT,
    };
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
    long canvas_width = config->canvas_output_size.w;
    long canvas_height = config->canvas_output_size.h;
    long fps = (long)config->target_fps;

    TgResult result = app_config_read_positive_long(
        &source, "tui_width", INT32_MAX, &width);
    if (tg_result_ok(result)) {
        result = app_config_read_positive_long(
            &source, "tui_height", INT32_MAX, &height);
    }
    if (tg_result_ok(result)) {
        result = app_config_read_positive_long(
            &source, "canvas_width", INT32_MAX, &canvas_width);
    }
    if (tg_result_ok(result)) {
        result = app_config_read_positive_long(
            &source, "canvas_height", INT32_MAX, &canvas_height);
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
        config->canvas_output_size = (TgSizei){
            (int32_t)canvas_width,
            (int32_t)canvas_height,
        };
        config->target_fps = (unsigned)fps;
    }

    config_free(&source);
    return result;
}

TgResult app_add_page(
    App *app,
    const char *title,
    TuiKey shortcut,
    const char *module_path,
    TgBytes config)
{
    if (app == NULL ||
        title == NULL ||
        module_path == NULL ||
        (config.len > 0 && config.data == NULL) ||
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

    uint8_t *config_storage = NULL;
    if (config.len > 0) {
        config_storage = malloc(config.len);
        if (config_storage == NULL) {
            free(cells);
            return TG_ERR_NOMEM;
        }
        memcpy(config_storage, config.data, config.len);
    }

    Page *page = &app->pages[app->page_count];
    *page = (Page){
        .title = title,
        .shortcut = shortcut,
        .module_path = module_path,
        .active = app->page_count == app->active_page,
        .entered = false,
        .frame = {
            .size = app->content_size,
            .cells = cells,
        },
        .config_storage = config_storage,
        .config = {
            .data = config_storage,
            .len = config.len,
        },
    };
    plugin_frame_clear(&page->frame);

    TgResult result = draw_plugin_file_stamp(
        page->module_path,
        &page->loaded_stamp);
    if (tg_result_ok(result)) {
        DrawPluginOpenArgs args = app_plugin_open_args(app, page);
        result = draw_plugin_module_open(
            page->module_path,
            &args,
            &page->module);
    }
    if (tg_result_err(result)) {
        free(page->config_storage);
        free(page->frame.cells);
        memset(page, 0, sizeof(*page));
        return result;
    }

    page->generation = 1;
    ++app->page_count;
    return TG_OK;
}

TgResult app_init(App *app, const AppConfig *config)
{
    if (app == NULL ||
        config == NULL ||
        config->screen_size.w <= 0 ||
        config->screen_size.h <= config->footer_height ||
        config->canvas_output_size.w <= 0 ||
        config->canvas_output_size.h <= 0 ||
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
    result = app_set_active_page(
        app,
        app->active_page,
        DRAW_PLUGIN_LEAVE_SWITCH);
    if (tg_result_err(result)) {
        app_shutdown(app);
        return result;
    }
    app->last_frame_ns = app_now_ns();
    return TG_OK;
}

void app_shutdown(App *app)
{
    if (app == NULL) {
        return;
    }

    if (app->active_page < app->page_count) {
        Page *active = &app->pages[app->active_page];
        if (active->entered) {
            DrawPluginLeaveReason reason = DRAW_PLUGIN_LEAVE_SHUTDOWN;
            (void)active->module.functions.write(
                active->module.instance,
                DRAW_PLUGIN_WRITE_LEAVE,
                &reason);
        }
        active->entered = false;
    }

    for (size_t i = app->page_count; i > 0; --i) {
        Page *page = &app->pages[i - 1u];
        draw_plugin_module_close(&page->module);
        free(page->config_storage);
        page->config_storage = NULL;
        page->config = (TgBytes){0};
        free(page->frame.cells);
        page->frame.cells = NULL;
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

TgResult app_begin_frame(App *app)
{
    if (app == NULL) {
        return TG_ERR_INVALID;
    }

    app_poll_plugin_reloads(app);

    app->frame_start_ns = app_now_ns();
    if (app->last_frame_ns > 0 && app->frame_start_ns >= app->last_frame_ns) {
        app->delta_time =
            (double)(app->frame_start_ns - app->last_frame_ns) / 1000000000.0;
    } else {
        app->delta_time = 0.0;
    }
    app->last_frame_ns = app->frame_start_ns;

    return tui_poll_events();
}

TgResult app_dispatch_events(App *app)
{
    if (app == NULL || app->page_count == 0) {
        return TG_ERR_INVALID;
    }

    const TuiInputEvent *events = tui_input_events();
    size_t event_count = tui_input_event_count();
    for (size_t event_index = 0;
         event_index < event_count;
         ++event_index) {
        const TuiInputEvent *event = &events[event_index];

        if (app_event_is_control_char(event, 'q')) {
            app->running = false;
            return TG_OK;
        }
        if (app_event_is_control_char(event, 'r')) {
            app_force_reload_active_page(app);
            continue;
        }

        bool switched = false;
        for (size_t page_index = 0;
             page_index < app->page_count;
             ++page_index) {
            if (app_event_is_plain_key(
                    event,
                    app->pages[page_index].shortcut)) {
                TgResult result = app_set_active_page(
                    app,
                    page_index,
                    DRAW_PLUGIN_LEAVE_SWITCH);
                if (tg_result_err(result)) {
                    return result;
                }
                switched = true;
                break;
            }
        }
        if (switched) {
            continue;
        }

        Page *active = &app->pages[app->active_page];
        TgResult result = active->module.functions.write(
            active->module.instance,
            DRAW_PLUGIN_WRITE_INPUT,
            event);
        if (tg_result_err(result)) {
            return result;
        }
    }
    return TG_OK;
}

TgResult app_update_active_page(App *app)
{
    if (app == NULL || app->active_page >= app->page_count) {
        return TG_ERR_INVALID;
    }

    DrawPluginFrameContext context = {
        .frame_index = app->frame_index,
        .delta_time = app->delta_time,
        .frame_size = app->content_size,
    };

    Page *active = &app->pages[app->active_page];
    return active->module.functions.write(
        active->module.instance,
        DRAW_PLUGIN_WRITE_TICK,
        &context);
}

TgResult app_render_active_page(App *app)
{
    if (app == NULL || app->active_page >= app->page_count) {
        return TG_ERR_INVALID;
    }

    Page *active = &app->pages[app->active_page];
    return active->module.functions.read(
        active->module.instance,
        DRAW_PLUGIN_READ_FRAME,
        &active->frame);
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
