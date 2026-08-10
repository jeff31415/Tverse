#include "draw_online/server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static volatile sig_atomic_t draw_demo_stop_requested;

static void draw_demo_handle_signal(int signal_number)
{
    (void)signal_number;
    draw_demo_stop_requested = 1;
}

int main(int argc, char **argv)
{
    DrawOnlineServerOptions options;
    DrawOnlineServer *server = NULL;
    struct sigaction action;
    struct timespec pause_duration;
    const char *module_path = DRAW_ONLINE_DEFAULT_ROOM_MODULE;
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
    if (draw_online_server_start(&server, &options) != 0) {
        fprintf(stderr, "failed to start demo server with %s\n", module_path);
        return 1;
    }
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
