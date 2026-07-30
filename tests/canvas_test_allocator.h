#ifndef DRAW_APP_TESTS_CANVAS_TEST_ALLOCATOR_H
#define DRAW_APP_TESTS_CANVAS_TEST_ALLOCATOR_H

#include <stddef.h>

void canvas_test_allocator_reset(void);
void canvas_test_allocator_fail_after(size_t successful_allocations);

void *canvas_test_malloc(size_t size);
void *canvas_test_calloc(size_t count, size_t size);
void *canvas_test_realloc(void *pointer, size_t size);
void canvas_test_free(void *pointer);

#endif
