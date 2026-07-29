#ifndef DRAW_APP_APP_H
#define DRAW_APP_APP_H

#include "tui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_MAX_PAGES 9u
#define APP_DEFAULT_WIDTH 120
#define APP_DEFAULT_HEIGHT 36
#define APP_DEFAULT_FPS 30u
#define APP_FOOTER_HEIGHT 1

typedef struct AppFrame {
    TgSizei size;
    TuiCell *cells;
} AppFrame;

typedef struct AppFrameContext {
    uint64_t frame_index;
    double delta_time;
} AppFrameContext;

typedef struct Page Page;
typedef void (*PageUpdateFn)(Page *page, const AppFrameContext *context);

struct Page {
    const char *title;
    TuiKey shortcut;
    bool active;
    AppFrame frame;
    PageUpdateFn update;
    void *userdata;
};

typedef struct AppConfig {
    TgSizei screen_size;
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
void app_begin_frame(App *app);
void app_update_pages(App *app);
void app_compose(App *app);
TgResult app_end_frame(App *app);

TgResult app_add_page(
    App *app,
    const char *title,
    TuiKey shortcut,
    PageUpdateFn update,
    void *userdata);

void app_frame_clear(AppFrame *frame);
void app_frame_draw_text(
    AppFrame *frame,
    int x,
    int y,
    const char *text,
    uint32_t fg,
    uint32_t bg,
    uint16_t style);

#endif
