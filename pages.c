#include "pages.h"

#include "canvas_page.h"

static TgResult simple_page_on_enter(Page *page)
{
    if (page == NULL) {
        return TG_ERR_INVALID;
    }
    app_frame_clear(&page->frame);
    return TG_OK;
}

static TgResult simple_page_on_leave(
    Page *page,
    PageLeaveReason reason)
{
    (void)reason;
    return page == NULL ? TG_ERR_INVALID : TG_OK;
}

static TgResult simple_page_handle_event(
    Page *page,
    const TuiInputEvent *event)
{
    return page == NULL || event == NULL ? TG_ERR_INVALID : TG_OK;
}

static TgResult simple_page_update(
    Page *page,
    const AppFrameContext *context)
{
    return page == NULL || context == NULL ? TG_ERR_INVALID : TG_OK;
}

static TgResult simple_page_render(Page *page)
{
    return page == NULL ? TG_ERR_INVALID : TG_OK;
}

static PageOps simple_page_ops(void)
{
    return (PageOps){
        .on_enter = simple_page_on_enter,
        .on_leave = simple_page_on_leave,
        .handle_event = simple_page_handle_event,
        .update = simple_page_update,
        .render = simple_page_render,
        .destroy = NULL,
    };
}

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
        PageOps ops = simple_page_ops();
        void *userdata = NULL;

        if (definitions[i].canvas) {
            CanvasPage *canvas = NULL;
            TgResult result = canvas_page_create(
                app->config.canvas_output_size,
                &canvas);
            if (tg_result_err(result)) {
                return result;
            }
            userdata = canvas;
            ops = canvas_page_ops();
        }

        TgResult result = app_add_page(
            app,
            definitions[i].title,
            definitions[i].shortcut,
            &ops,
            userdata);
        if (tg_result_err(result)) {
            if (definitions[i].canvas) {
                Page temporary = {
                    .ops = ops,
                    .userdata = userdata,
                };
                ops.destroy(&temporary);
            }
            return result;
        }
    }
    return TG_OK;
}
