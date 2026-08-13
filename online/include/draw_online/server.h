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
    /*
     * Opaque configuration handed to the room module through
     * DrawRoomContext.config. The server copies these bytes, so the caller's
     * buffer need not outlive the call. A null pointer or zero length leaves
     * the room's config empty. The copy is NUL-terminated one byte past
     * room_config_length so a room may treat text configuration as a C string
     * without copying again; that terminator is not counted in the length.
     */
    const void *room_config;
    size_t room_config_length;
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
