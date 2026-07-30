#include "canvas_test_allocator.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static size_t canvas_test_allocations_before_failure = SIZE_MAX;

static bool canvas_test_allocation_should_fail(void)
{
    if (canvas_test_allocations_before_failure == SIZE_MAX) {
        return false;
    }
    if (canvas_test_allocations_before_failure == 0) {
        return true;
    }

    --canvas_test_allocations_before_failure;
    return false;
}

void canvas_test_allocator_reset(void)
{
    canvas_test_allocations_before_failure = SIZE_MAX;
}

void canvas_test_allocator_fail_after(size_t successful_allocations)
{
    canvas_test_allocations_before_failure = successful_allocations;
}

void *canvas_test_malloc(size_t size)
{
    if (canvas_test_allocation_should_fail()) {
        return NULL;
    }
    return malloc(size);
}

void *canvas_test_calloc(size_t count, size_t size)
{
    if (canvas_test_allocation_should_fail()) {
        return NULL;
    }
    return calloc(count, size);
}

void *canvas_test_realloc(void *pointer, size_t size)
{
    if (canvas_test_allocation_should_fail()) {
        return NULL;
    }
    return realloc(pointer, size);
}

void canvas_test_free(void *pointer)
{
    free(pointer);
}
