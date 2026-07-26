#include "app.h"

#include <stdio.h>

int main(int argc, char *argv[])
{
    if (argc > 2) {
        fprintf(stderr, "usage: %s [config-file]\n", argv[0]);
        return 2;
    }

    AppConfig config;
    app_config_defaults(&config);
    if (argc == 2) {
        TgResult config_result = app_config_load(&config, argv[1]);
        if (tg_result_err(config_result)) {
            return 1;
        }
    }

    App app;
    TgResult result = app_init(&app, &config);
    if (tg_result_err(result)) {
        fprintf(stderr, "draw_app initialization failed: %d\n", result);
        return 1;
    }

    while (!app_should_close(&app)) {
        app_begin_frame(&app);
        if (app_should_close(&app)) {
            break;
        }
        app_update_pages(&app);
        app_compose(&app);
        result = app_end_frame(&app);
        if (tg_result_err(result)) {
            break;
        }
    }

    app_shutdown(&app);
    if (tg_result_err(result)) {
        fprintf(stderr, "draw_app frame failed: %d\n", result);
        return 1;
    }
    return 0;
}
