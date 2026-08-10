#ifndef DRAW_ONLINE_SERVER_H
#define DRAW_ONLINE_SERVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DrawOnlineServer DrawOnlineServer;

typedef struct DrawOnlineServerOptions {
    const char *bind_host;
    uint16_t port;
    const char *room_module_path;
    size_t queue_records;
    size_t queue_bytes;
} DrawOnlineServerOptions;

int draw_online_server_start(
    DrawOnlineServer **out_server,
    const DrawOnlineServerOptions *options);

uint16_t draw_online_server_port(const DrawOnlineServer *server);
void draw_online_server_stop(DrawOnlineServer *server);
void draw_online_server_destroy(DrawOnlineServer *server);

#ifdef __cplusplus
}
#endif

#endif
