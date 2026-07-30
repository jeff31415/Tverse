#ifndef DRAW_APP_CANVAS_PAGE_H
#define DRAW_APP_CANVAS_PAGE_H

#include "app.h"

typedef struct CanvasPage CanvasPage;

TgResult canvas_page_create(
    TgSizei output_size,
    CanvasPage **out_page);

PageOps canvas_page_ops(void);

#endif
