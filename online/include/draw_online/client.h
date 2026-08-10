#ifndef DRAW_ONLINE_CLIENT_H
#define DRAW_ONLINE_CLIENT_H

#include "draw_online/wire.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DrawOnlineClient DrawOnlineClient;

typedef struct DrawOnlineClientEvent {
    DrawWireHeader header;
    uint8_t *payload;
    size_t payload_length;
} DrawOnlineClientEvent;

int draw_online_client_connect(
    DrawOnlineClient **out_client,
    const char *host,
    uint16_t port,
    int timeout_ms);

int draw_online_client_login(DrawOnlineClient *client, int timeout_ms);
int draw_online_client_join(
    DrawOnlineClient *client,
    uint64_t room_id,
    uint32_t *out_player_slot,
    int timeout_ms);

int draw_online_client_send_game(
    DrawOnlineClient *client,
    uint64_t room_id,
    const void *payload,
    size_t payload_length,
    int timeout_ms);

int draw_online_client_receive(
    DrawOnlineClient *client,
    DrawOnlineClientEvent *out_event,
    int timeout_ms);

void draw_online_client_event_release(DrawOnlineClientEvent *event);
uint64_t draw_online_client_uid(const DrawOnlineClient *client);
void draw_online_client_close(DrawOnlineClient *client);

#ifdef __cplusplus
}
#endif

#endif
