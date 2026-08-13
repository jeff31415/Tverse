#include "plugin_loader.h"
#include "unity.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef TEST_PLUGIN_PATH
#error "TEST_PLUGIN_PATH must name the example plugin module"
#endif
#ifndef TEST_CANVAS_PLUGIN_PATH
#error "TEST_CANVAS_PLUGIN_PATH must name the Canvas plugin module"
#endif
#ifndef TEST_TEMPLETE_PLUGIN_PATH
#error "TEST_TEMPLETE_PLUGIN_PATH must name the starter template module"
#endif

typedef struct FakeStdin {
    const unsigned char *bytes;
    size_t length;
    size_t offset;
    size_t calls;
} FakeStdin;

static size_t fake_stdin_read(
    void *userdata,
    void *destination,
    size_t capacity)
{
    FakeStdin *input = userdata;
    ++input->calls;
    size_t remaining = input->length - input->offset;
    size_t count = remaining < capacity ? remaining : capacity;
    memcpy(destination, input->bytes + input->offset, count);
    input->offset += count;
    return count;
}

static bool surface_contains(
    const DrawPluginSurface *surface,
    const char *needle)
{
    size_t needle_length = strlen(needle);
    if (needle_length == 0 || needle_length > (size_t)surface->size.w) {
        return false;
    }

    for (int y = 0; y < surface->size.h; ++y) {
        for (int x = 0;
             x <= surface->size.w - (int)needle_length;
             ++x) {
            bool match = true;
            for (size_t i = 0; i < needle_length; ++i) {
                size_t index =
                    (size_t)y * (size_t)surface->size.w +
                    (size_t)x + i;
                if (surface->cells[index].ch[0] != needle[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
    }
    return false;
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_example_plugin_full_abi_and_generation_cleanup(void)
{
    static const unsigned char raw_input[] = {'a', 'b', 'c'};
    static const char title[] = "Loader test page";
    FakeStdin input = {
        .bytes = raw_input,
        .length = sizeof(raw_input),
    };
    DrawPluginOpenArgs args = {
        .abi_version = DRAW_PLUGIN_ABI_VERSION + 1u,
        .frame_size = {80, 20},
        .config = {
            .data = (const uint8_t *)title,
            .len = sizeof(title) - 1u,
        },
        .host = {
            .userdata = &input,
            .stdin_read = fake_stdin_read,
        },
    };

    DrawPluginModule module;
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_UNSUPPORTED,
        draw_plugin_module_open(TEST_PLUGIN_PATH, &args, &module));
    TEST_ASSERT_NULL(module.handle);
    TEST_ASSERT_NULL(module.instance);

    args.abi_version = DRAW_PLUGIN_ABI_VERSION;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        draw_plugin_module_open(TEST_PLUGIN_PATH, &args, &module));
    TEST_ASSERT_NOT_NULL(module.handle);
    TEST_ASSERT_NOT_NULL(module.instance);
    TEST_ASSERT_NOT_NULL(module.generation_path);
    TEST_ASSERT_NOT_EQUAL_STRING(TEST_PLUGIN_PATH, module.generation_path);
    TEST_ASSERT_EQUAL_INT(0, access(module.generation_path, F_OK));

    (void)dlerror();
    TEST_ASSERT_NULL(dlsym(module.handle, "plugin_frame_put"));
    TEST_ASSERT_NOT_NULL(dlerror());

    DrawPluginModule second_generation;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        draw_plugin_module_open(
            TEST_PLUGIN_PATH,
            &args,
            &second_generation));
    TEST_ASSERT_NOT_EQUAL_STRING(
        module.generation_path,
        second_generation.generation_path);
    draw_plugin_module_close(&second_generation);

    int invalid_payload = 0;
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_ENTER,
            &invalid_payload));

    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_ENTER,
            NULL));

    TuiInputEvent event;
    memset(&event, 0, sizeof(event));
    event.type = TUI_INPUT_TEXT;
    event.ch = 'Z';
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_INPUT,
            &event));

    DrawPluginFrameContext context = {
        .frame_index = 7,
        .delta_time = 1.0 / 30.0,
        .frame_size = args.frame_size,
    };
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_TICK,
            &context));
    TEST_ASSERT_EQUAL_size_t(1, input.calls);
    TEST_ASSERT_EQUAL_size_t(sizeof(raw_input), input.offset);
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_TICK,
            &context));
    TEST_ASSERT_EQUAL_size_t(2, input.calls);
    TEST_ASSERT_EQUAL_size_t(sizeof(raw_input), input.offset);

    size_t cell_count =
        (size_t)args.frame_size.w * (size_t)args.frame_size.h;
    TuiCell *cells = calloc(cell_count, sizeof(*cells));
    TEST_ASSERT_NOT_NULL(cells);
    DrawPluginSurface surface = {
        .size = args.frame_size,
        .cells = cells,
    };
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.read(
            module.instance,
            DRAW_PLUGIN_READ_FRAME,
            &surface));
    TEST_ASSERT_TRUE(surface_contains(&surface, title));
    TEST_ASSERT_TRUE(surface_contains(&surface, "ticks=2"));
    TEST_ASSERT_TRUE(surface_contains(&surface, "raw stdin bytes=3"));
    TEST_ASSERT_TRUE(surface_contains(&surface, "ch=90"));
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_INVALID,
        module.functions.read(
            module.instance,
            (DrawPluginReadKind)99,
            &surface));
    free(cells);

    DrawPluginLeaveReason reason = DRAW_PLUGIN_LEAVE_SHUTDOWN;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_LEAVE,
            &reason));

    size_t generation_length = strlen(module.generation_path);
    char *generation_path = malloc(generation_length + 1u);
    TEST_ASSERT_NOT_NULL(generation_path);
    memcpy(
        generation_path,
        module.generation_path,
        generation_length + 1u);

    draw_plugin_module_close(&module);
    TEST_ASSERT_NULL(module.handle);
    TEST_ASSERT_NULL(module.instance);
    TEST_ASSERT_NULL(module.generation_path);
    TEST_ASSERT_NOT_EQUAL(0, access(generation_path, F_OK));
    free(generation_path);

    draw_plugin_module_close(&module);
}

static void test_canvas_plugin_uses_the_same_abi(void)
{
    TgSizei output_size = {32, 12};
    DrawPluginOpenArgs args = {
        .abi_version = DRAW_PLUGIN_ABI_VERSION,
        .frame_size = {100, 30},
        .config = {
            .data = (const uint8_t *)&output_size,
            .len = sizeof(output_size),
        },
    };
    DrawPluginModule module;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        draw_plugin_module_open(
            TEST_CANVAS_PLUGIN_PATH,
            &args,
            &module));

    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_ENTER,
            NULL));
    DrawPluginFrameContext context = {
        .frame_size = args.frame_size,
    };
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_TICK,
            &context));

    size_t cell_count =
        (size_t)args.frame_size.w * (size_t)args.frame_size.h;
    TuiCell *cells = calloc(cell_count, sizeof(*cells));
    TEST_ASSERT_NOT_NULL(cells);
    DrawPluginSurface surface = {
        .size = args.frame_size,
        .cells = cells,
    };
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.read(
            module.instance,
            DRAW_PLUGIN_READ_FRAME,
            &surface));
    TEST_ASSERT_TRUE(surface_contains(&surface, "Canvas"));
    TEST_ASSERT_TRUE(surface_contains(&surface, "Palette"));
    free(cells);

    DrawPluginLeaveReason reason = DRAW_PLUGIN_LEAVE_SHUTDOWN;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_LEAVE,
            &reason));
    draw_plugin_module_close(&module);
}

static void test_plugin_templete_is_a_complete_loadable_module(void)
{
    static const unsigned char raw_input[] = {'r', 'a', 'w'};
    static const char title[] = "Starter test";
    FakeStdin input = {
        .bytes = raw_input,
        .length = sizeof(raw_input),
    };
    DrawPluginOpenArgs args = {
        .abi_version = DRAW_PLUGIN_ABI_VERSION,
        .frame_size = {60, 12},
        .config = {
            .data = (const uint8_t *)title,
            .len = sizeof(title) - 1u,
        },
        .host = {
            .userdata = &input,
            .stdin_read = fake_stdin_read,
        },
    };

    DrawPluginModule module;
    args.abi_version = DRAW_PLUGIN_ABI_VERSION + 1u;
    TEST_ASSERT_EQUAL_INT(
        TG_ERR_UNSUPPORTED,
        draw_plugin_module_open(
            TEST_TEMPLETE_PLUGIN_PATH,
            &args,
            &module));
    TEST_ASSERT_NULL(module.handle);
    TEST_ASSERT_NULL(module.instance);

    args.abi_version = DRAW_PLUGIN_ABI_VERSION;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        draw_plugin_module_open(
            TEST_TEMPLETE_PLUGIN_PATH,
            &args,
            &module));

    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_ENTER,
            NULL));

    TuiInputEvent event;
    memset(&event, 0, sizeof(event));
    event.type = TUI_INPUT_TEXT;
    event.ch = 'T';
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_INPUT,
            &event));

    DrawPluginFrameContext context = {
        .frame_index = 11,
        .delta_time = 1.0 / 30.0,
        .frame_size = args.frame_size,
    };
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_TICK,
            &context));
    TEST_ASSERT_EQUAL_size_t(1, input.calls);
    TEST_ASSERT_EQUAL_size_t(sizeof(raw_input), input.offset);

    size_t cell_count =
        (size_t)args.frame_size.w * (size_t)args.frame_size.h;
    TuiCell *cells = calloc(cell_count, sizeof(*cells));
    TEST_ASSERT_NOT_NULL(cells);
    DrawPluginSurface surface = {
        .size = args.frame_size,
        .cells = cells,
    };
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.read(
            module.instance,
            DRAW_PLUGIN_READ_FRAME,
            &surface));
    TEST_ASSERT_TRUE(surface_contains(&surface, title));
    TEST_ASSERT_TRUE(surface_contains(&surface, "frame=11"));
    TEST_ASSERT_TRUE(surface_contains(&surface, "raw-bytes=3"));
    TEST_ASSERT_TRUE(surface_contains(&surface, "ch=84"));
    free(cells);

    DrawPluginLeaveReason reason = DRAW_PLUGIN_LEAVE_SHUTDOWN;
    TEST_ASSERT_EQUAL_INT(
        TG_OK,
        module.functions.write(
            module.instance,
            DRAW_PLUGIN_WRITE_LEAVE,
            &reason));
    draw_plugin_module_close(&module);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_example_plugin_full_abi_and_generation_cleanup);
    RUN_TEST(test_canvas_plugin_uses_the_same_abi);
    RUN_TEST(test_plugin_templete_is_a_complete_loadable_module);
    return UNITY_END();
}
