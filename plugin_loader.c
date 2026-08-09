#define _POSIX_C_SOURCE 200809L

#include "plugin_loader.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static TgResult draw_plugin_copy_generation(
    const char *source_path,
    char **out_path)
{
    if (source_path == NULL || out_path == NULL) {
        return TG_ERR_INVALID;
    }
    *out_path = NULL;

    const char *temporary_directory = getenv("TMPDIR");
    if (temporary_directory == NULL || temporary_directory[0] == '\0') {
        temporary_directory = "/tmp";
    }
    const char *basename = strrchr(source_path, '/');
    basename = basename == NULL ? source_path : basename + 1;

    static const char prefix[] = "/draw-app-";
    static const char suffix[] = "-XXXXXX";
    size_t directory_length = strlen(temporary_directory);
    size_t basename_length = strlen(basename);
    if (directory_length > SIZE_MAX - sizeof(prefix) ||
        basename_length >
            SIZE_MAX - directory_length - sizeof(prefix) - sizeof(suffix)) {
        return TG_ERR_INVALID;
    }

    size_t path_size =
        directory_length + sizeof(prefix) - 1u +
        basename_length + sizeof(suffix);
    char *path = malloc(path_size);
    if (path == NULL) {
        return TG_ERR_NOMEM;
    }
    (void)snprintf(
        path,
        path_size,
        "%s%s%s%s",
        temporary_directory,
        prefix,
        basename,
        suffix);

    int destination = mkstemp(path);
    if (destination < 0) {
        free(path);
        return TG_ERR;
    }

    int source = open(source_path, O_RDONLY);
    if (source < 0) {
        close(destination);
        unlink(path);
        free(path);
        return TG_ERR_NOT_FOUND;
    }

    TgResult result = TG_OK;
    unsigned char buffer[16384];
    for (;;) {
        ssize_t read_count = read(source, buffer, sizeof(buffer));
        if (read_count == 0) {
            break;
        }
        if (read_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            result = TG_ERR;
            break;
        }

        ssize_t written = 0;
        while (written < read_count) {
            ssize_t write_count = write(
                destination,
                buffer + written,
                (size_t)(read_count - written));
            if (write_count < 0 && errno == EINTR) {
                continue;
            }
            if (write_count <= 0) {
                result = TG_ERR;
                break;
            }
            written += write_count;
        }
        if (tg_result_err(result)) {
            break;
        }
    }

    if (close(source) != 0 && tg_result_ok(result)) {
        result = TG_ERR;
    }
    if (close(destination) != 0 && tg_result_ok(result)) {
        result = TG_ERR;
    }
    if (tg_result_err(result)) {
        unlink(path);
        free(path);
        return result;
    }

    *out_path = path;
    return TG_OK;
}

static TgResult draw_plugin_symbol(
    void *handle,
    const char *name,
    void *out_function,
    size_t function_size)
{
    if (handle == NULL || name == NULL || out_function == NULL ||
        function_size != sizeof(void *)) {
        return TG_ERR_UNSUPPORTED;
    }

    (void)dlerror();
    void *symbol = dlsym(handle, name);
    const char *error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "draw_app: missing plugin symbol %s: %s\n", name, error);
        return TG_ERR_NOT_FOUND;
    }

    memcpy(out_function, &symbol, sizeof(symbol));
    return TG_OK;
}

TgResult draw_plugin_file_stamp(
    const char *path,
    DrawPluginFileStamp *out_stamp)
{
    if (path == NULL || out_stamp == NULL) {
        return TG_ERR_INVALID;
    }

    struct stat status;
    if (stat(path, &status) != 0 || status.st_size <= 0) {
        return TG_ERR_NOT_FOUND;
    }

    out_stamp->modified_ns =
        (int64_t)status.st_mtim.tv_sec * 1000000000ll +
        (int64_t)status.st_mtim.tv_nsec;
    out_stamp->size = (uint64_t)status.st_size;
    return TG_OK;
}

bool draw_plugin_file_stamp_equal(
    DrawPluginFileStamp left,
    DrawPluginFileStamp right)
{
    return left.modified_ns == right.modified_ns && left.size == right.size;
}

TgResult draw_plugin_module_open(
    const char *source_path,
    const DrawPluginOpenArgs *args,
    DrawPluginModule *out_module)
{
    if (source_path == NULL || args == NULL || out_module == NULL) {
        return TG_ERR_INVALID;
    }
    memset(out_module, 0, sizeof(*out_module));

    char *generation_path = NULL;
    TgResult result = draw_plugin_copy_generation(
        source_path, &generation_path);
    if (tg_result_err(result)) {
        return result;
    }

    void *handle = dlopen(generation_path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "draw_app: dlopen %s failed: %s\n",
            source_path, dlerror());
        unlink(generation_path);
        free(generation_path);
        return TG_ERR;
    }

    DrawPluginFunctions functions;
    memset(&functions, 0, sizeof(functions));
    result = draw_plugin_symbol(
        handle, "draw_plugin_entry",
        &functions.entry, sizeof(functions.entry));
    if (tg_result_ok(result)) {
        result = draw_plugin_symbol(
            handle, "draw_plugin_cleanup",
            &functions.cleanup, sizeof(functions.cleanup));
    }
    if (tg_result_ok(result)) {
        result = draw_plugin_symbol(
            handle, "draw_plugin_write",
            &functions.write, sizeof(functions.write));
    }
    if (tg_result_ok(result)) {
        result = draw_plugin_symbol(
            handle, "draw_plugin_read",
            &functions.read, sizeof(functions.read));
    }
    if (tg_result_err(result)) {
        dlclose(handle);
        unlink(generation_path);
        free(generation_path);
        return result;
    }

    DrawPlugin *instance = NULL;
    result = functions.entry(args, &instance);
    if (tg_result_err(result) || instance == NULL) {
        if (instance != NULL) {
            functions.cleanup(instance);
        }
        dlclose(handle);
        unlink(generation_path);
        free(generation_path);
        return tg_result_err(result) ? result : TG_ERR;
    }

    *out_module = (DrawPluginModule){
        .handle = handle,
        .instance = instance,
        .functions = functions,
        .generation_path = generation_path,
    };
    return TG_OK;
}

void draw_plugin_module_close(DrawPluginModule *module)
{
    if (module == NULL) {
        return;
    }

    if (module->instance != NULL && module->functions.cleanup != NULL) {
        module->functions.cleanup(module->instance);
    }
    module->instance = NULL;

    if (module->handle != NULL) {
        if (dlclose(module->handle) != 0) {
            fprintf(stderr, "draw_app: dlclose failed: %s\n", dlerror());
        }
    }
    module->handle = NULL;
    memset(&module->functions, 0, sizeof(module->functions));

    if (module->generation_path != NULL) {
        unlink(module->generation_path);
        free(module->generation_path);
        module->generation_path = NULL;
    }
}
