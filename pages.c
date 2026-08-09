#include "pages.h"

#include <string.h>

#ifndef DRAW_APP_PLUGIN_DIR
#define DRAW_APP_PLUGIN_DIR "plugins"
#endif

#define DRAW_APP_CANVAS_PLUGIN \
    DRAW_APP_PLUGIN_DIR "/canvas_page.so"
#define DRAW_APP_EXAMPLE_PLUGIN \
    DRAW_APP_PLUGIN_DIR "/example_page.so"

TgResult app_register_default_pages(App *app)
{
    if (app == NULL) {
        return TG_ERR_INVALID;
    }

    static const struct {
        const char *title;
        TuiKey shortcut;
        bool canvas;
    } definitions[] = {
        {"Page 1", TUI_KEY_F1, false},
        {"Canvas", TUI_KEY_F2, true},
        {"Page 3", TUI_KEY_F3, false},
        {"Page 4", TUI_KEY_F4, false},
        {"Page 5", TUI_KEY_F5, false},
        {"Page 6", TUI_KEY_F6, false},
        {"Page 7", TUI_KEY_F7, false},
        {"Page 8", TUI_KEY_F8, false},
        {"Page 9", TUI_KEY_F9, false},
    };

    for (size_t i = 0;
         i < sizeof(definitions) / sizeof(definitions[0]);
         ++i) {
        const char *module_path = definitions[i].canvas
            ? DRAW_APP_CANVAS_PLUGIN
            : DRAW_APP_EXAMPLE_PLUGIN;
        TgBytes config;
        if (definitions[i].canvas) {
            config = (TgBytes){
                .data = (const uint8_t *)&app->config.canvas_output_size,
                .len = sizeof(app->config.canvas_output_size),
            };
        } else {
            config = (TgBytes){
                .data = (const uint8_t *)definitions[i].title,
                .len = strlen(definitions[i].title),
            };
        }

        TgResult result = app_add_page(
            app,
            definitions[i].title,
            definitions[i].shortcut,
            module_path,
            config);
        if (tg_result_err(result)) {
            return result;
        }
    }
    return TG_OK;
}
