#ifndef DRAW_APP_TESTS_CANVAS_TEST_HOOKS_H
#define DRAW_APP_TESTS_CANVAS_TEST_HOOKS_H

#include "canvas.h"

/*
 * Test-only corruption hook. Production targets do not define
 * CANVAS_TESTING and therefore do not export this function.
 */
bool canvas_test_history_make_cycle(CanvasDocument *document);
bool canvas_test_history_break_cycle(CanvasDocument *document);

#endif
