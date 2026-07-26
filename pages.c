#include "pages.h"

static void page_f1_update(Page *page, const AppFrameContext *context)
{
    (void)context;
    if (!page->active) {
        return;
    }
    /* F1 page implementation goes here. */
}

static void page_f2_update(Page *page, const AppFrameContext *context)
{
    (void)context;
    if (!page->active) {
        return;
    }
    /* F2 page implementation goes here. */
}

static void page_f3_update(Page *page, const AppFrameContext *context)
{
    (void)context;
    if (!page->active) {
        return;
    }
    /* F3 page implementation goes here. */
}

static void page_f4_update(Page *page, const AppFrameContext *context)
{
    (void)context;
    if (!page->active) {
        return;
    }
    /* F4 page implementation goes here. */
}

static void page_f5_update(Page *page, const AppFrameContext *context)
{
    (void)context;
    if (!page->active) {
        return;
    }
    /* F5 page implementation goes here. */
}

static void page_f6_update(Page *page, const AppFrameContext *context)
{
    (void)context;
    if (!page->active) {
        return;
    }
    /* F6 page implementation goes here. */
}

static void page_f7_update(Page *page, const AppFrameContext *context)
{
    (void)context;
    if (!page->active) {
        return;
    }
    /* F7 page implementation goes here. */
}

static void page_f8_update(Page *page, const AppFrameContext *context)
{
    (void)context;
    if (!page->active) {
        return;
    }
    /* F8 page implementation goes here. */
}

static void page_f9_update(Page *page, const AppFrameContext *context)
{
    (void)context;
    if (!page->active) {
        return;
    }
    /* F9 page implementation goes here. */
}

TgResult app_register_default_pages(App *app)
{
    static const struct {
        const char *title;
        TuiKey shortcut;
        PageUpdateFn update;
    } definitions[] = {
        {"Page 1", TUI_KEY_F1, page_f1_update},
        {"Page 2", TUI_KEY_F2, page_f2_update},
        {"Page 3", TUI_KEY_F3, page_f3_update},
        {"Page 4", TUI_KEY_F4, page_f4_update},
        {"Page 5", TUI_KEY_F5, page_f5_update},
        {"Page 6", TUI_KEY_F6, page_f6_update},
        {"Page 7", TUI_KEY_F7, page_f7_update},
        {"Page 8", TUI_KEY_F8, page_f8_update},
        {"Page 9", TUI_KEY_F9, page_f9_update},
    };

    for (size_t i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i) {
        TgResult result = app_add_page(
            app,
            definitions[i].title,
            definitions[i].shortcut,
            definitions[i].update,
            NULL);
        if (tg_result_err(result)) {
            return result;
        }
    }
    return TG_OK;
}
