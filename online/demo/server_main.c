#include "draw_online/server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DRAW_DEMO_MAX_CONFIG_BYTES (64u * 1024u)

static volatile sig_atomic_t draw_demo_stop_requested;

static void draw_demo_handle_signal(int signal_number)
{
    (void)signal_number;
    draw_demo_stop_requested = 1;
}

/*
 * Reads at most DRAW_DEMO_MAX_CONFIG_BYTES into a fresh buffer. Returns 0 and
 * leaves *out_bytes NULL when path is NULL.
 */
static int draw_demo_read_config(
    const char *path,
    unsigned char **out_bytes,
    size_t *out_length)
{
    FILE *file;
    unsigned char *bytes;
    size_t length;

    *out_bytes = NULL;
    *out_length = 0u;
    if (path == NULL) {
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "cannot open room config: %s\n", path);
        return -1;
    }
    bytes = malloc(DRAW_DEMO_MAX_CONFIG_BYTES);
    if (bytes == NULL) {
        (void)fclose(file);
        return -1;
    }
    length = fread(bytes, 1u, DRAW_DEMO_MAX_CONFIG_BYTES, file);
    if (ferror(file) != 0 || feof(file) == 0) {
        fprintf(stderr, "room config unreadable or larger than %u bytes: %s\n",
            (unsigned)DRAW_DEMO_MAX_CONFIG_BYTES, path);
        free(bytes);
        (void)fclose(file);
        return -1;
    }
    (void)fclose(file);
    *out_bytes = bytes;
    *out_length = length;
    return 0;
}

int main(int argc, char **argv)
{
    DrawOnlineServerOptions options;
    DrawOnlineServer *server = NULL;
    struct sigaction action;
    struct timespec pause_duration;
    const char *module_path = DRAW_ONLINE_DEFAULT_ROOM_MODULE;
    const char *config_path = NULL;
    unsigned char *config_bytes = NULL;
    size_t config_length = 0u;
    unsigned long port_value = 0ul;

    if (argc > 1) {
        module_path = argv[1];
    }
    if (argc > 2) {
        char *end = NULL;
        port_value = strtoul(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || port_value > 65535ul) {
            fprintf(stderr, "invalid port: %s\n", argv[2]);
            return 2;
        }
    }
    if (argc > 3) {
        config_path = argv[3];
    }
    if (draw_demo_read_config(config_path, &config_bytes, &config_length) != 0) {
        return 2;
    }

    action.sa_handler = draw_demo_handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGINT, &action, NULL) != 0
        || sigaction(SIGTERM, &action, NULL) != 0) {
        perror("sigaction");
        return 1;
    }

    options.bind_host = "127.0.0.1";
    options.port = (uint16_t)port_value;
    options.room_module_path = module_path;
    options.queue_records = 0u;
    options.queue_bytes = 0u;
    options.room_config = config_bytes;
    options.room_config_length = config_length;
    if (draw_online_server_start(&server, &options) != 0) {
        fprintf(stderr, "failed to start demo server with %s\n", module_path);
        free(config_bytes);
        return 1;
    }
    /* The server copied the config; the demo's buffer is no longer needed. */
    free(config_bytes);
    printf("READY %u\n", (unsigned)draw_online_server_port(server));
    fflush(stdout);

    pause_duration.tv_sec = 0;
    pause_duration.tv_nsec = 100000000L;
    while (!draw_demo_stop_requested) {
        (void)nanosleep(&pause_duration, NULL);
    }
    draw_online_server_destroy(server);
    return 0;
}
