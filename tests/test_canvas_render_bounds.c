#include "canvas_state.h"
#include "unity.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

enum {
    FRAME_WIDTH = 4,
    FRAME_HEIGHT = 3
};

typedef struct GuardedCells {
    uint64_t prefix;
    TuiCell cells[FRAME_WIDTH * FRAME_HEIGHT];
    uint64_t suffix;
} GuardedCells;

static const uint64_t PREFIX_CANARY = UINT64_C(0x13579BDF2468ACE0);
static const uint64_t SUFFIX_CANARY = UINT64_C(0x0ECA8642FDB97531);

void setUp(void)
{
}

void tearDown(void)
{
}

static CanvasRenderTarget target_for(
    GuardedCells *guarded,
    TgRecti viewport_rect)
{
    CanvasRenderTarget target = {
        .size = {FRAME_WIDTH, FRAME_HEIGHT},
        .cells = guarded->cells,
        .viewport = {
            .rect = viewport_rect,
            .origin_screen = {0, 0},
        },
        .style = {
            .default_fg = 0x010203u,
            .draft_bg = 0x111213u,
            .output_bg = 0x212223u,
        },
    };
    return target;
}

static void initialize_guards(GuardedCells *guarded)
{
    memset(guarded, 0xA5, sizeof(*guarded));
    guarded->prefix = PREFIX_CANARY;
    guarded->suffix = SUFFIX_CANARY;
}

static void assert_guards_intact(const GuardedCells *guarded)
{
    TEST_ASSERT_EQUAL_HEX64(PREFIX_CANARY, guarded->prefix);
    TEST_ASSERT_EQUAL_HEX64(SUFFIX_CANARY, guarded->suffix);
}

static void test_clipped_render_preserves_canaries(void)
{
    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){3, 3}));
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_begin_stroke(
            &state,
            (TgVec2i){INT32_MAX, INT32_MIN},
            (TuiCell){.ch = {'X'}, .width = 1}));

    static const TgRecti viewports[] = {
        {-2, -2, 4, 4},
        {FRAME_WIDTH - 1, FRAME_HEIGHT - 1, INT32_MAX, INT32_MAX},
        {INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX},
        {INT32_MAX, INT32_MAX, 0, 0},
        {0, 0, FRAME_WIDTH, FRAME_HEIGHT},
    };

    for (size_t index = 0;
         index < sizeof(viewports) / sizeof(viewports[0]);
         ++index) {
        GuardedCells guarded;
        initialize_guards(&guarded);
        CanvasRenderTarget target = target_for(&guarded, viewports[index]);
        TEST_ASSERT_EQUAL_INT(
            TG_OK,
            canvas_state_render(&state, &target));
        assert_guards_intact(&guarded);
    }

    canvas_state_destroy(&state);
}

static void test_invalid_render_does_not_touch_buffer(void)
{
    CanvasState state;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        canvas_state_init(&state, (TgSizei){3, 3}));

    GuardedCells guarded;
    initialize_guards(&guarded);
    GuardedCells before = guarded;
    CanvasRenderTarget target =
        target_for(&guarded, (TgRecti){0, 0, -1, 1});
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_state_render(&state, &target));
    TEST_ASSERT_EQUAL_MEMORY(&before, &guarded, sizeof(guarded));

    target = target_for(&guarded, (TgRecti){0, 0, 1, 1});
    target.size.w = 0;
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        canvas_state_render(&state, &target));
    TEST_ASSERT_EQUAL_MEMORY(&before, &guarded, sizeof(guarded));
    assert_guards_intact(&guarded);
    canvas_state_destroy(&state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_clipped_render_preserves_canaries);
    RUN_TEST(test_invalid_render_does_not_touch_buffer);
    return UNITY_END();
}
