#ifndef DRAW_APP_APP_H
#define DRAW_APP_APP_H

#include "plugin_loader.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_MAX_PAGES 9u
#define APP_DEFAULT_WIDTH 120
#define APP_DEFAULT_HEIGHT 36
#define APP_DEFAULT_CANVAS_WIDTH 48
#define APP_DEFAULT_CANVAS_HEIGHT 20
#define APP_DEFAULT_FPS 30u
#define APP_FOOTER_HEIGHT 1

typedef struct Page Page;

struct Page {
    const char *title;
    TuiKey shortcut;
    const char *module_path;
    bool active;
    bool entered;
    DrawPluginSurface frame;
    uint8_t *config_storage;
    TgBytes config;
    DrawPluginModule module;
    DrawPluginFileStamp loaded_stamp;
    DrawPluginFileStamp pending_stamp;
    DrawPluginFileStamp attempted_stamp;
    uint64_t generation;
    unsigned pending_observations;
    bool pending_valid;
    bool attempted_valid;
};

typedef struct AppConfig {
    TgSizei screen_size;
    TgSizei canvas_output_size;
    unsigned target_fps;
    int footer_height;
} AppConfig;

typedef struct App {
    AppConfig config;
    TgSizei content_size;
    Page pages[APP_MAX_PAGES];
    size_t page_count;
    size_t active_page;
    bool running;
    bool tui_ready;
    uint64_t frame_index;
    int64_t frame_start_ns;
    int64_t last_frame_ns;
    double delta_time;
} App;

void app_config_defaults(AppConfig *config);
TgResult app_config_load(AppConfig *config, const char *path);

TgResult app_init(App *app, const AppConfig *config);
void app_shutdown(App *app);

bool app_should_close(const App *app);
TgResult app_begin_frame(App *app);
TgResult app_dispatch_events(App *app);
TgResult app_update_active_page(App *app);
TgResult app_render_active_page(App *app);
void app_compose(App *app);
TgResult app_end_frame(App *app);

TgResult app_add_page(
    App *app,
    const char *title,
    TuiKey shortcut,
    const char *module_path,
    TgBytes config);

#endif
