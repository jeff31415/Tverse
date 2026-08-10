#include "draw_online/client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    DrawOnlineClient *client = NULL;
    DrawOnlineClientEvent event;
    uint32_t player_slot = 0u;
    unsigned long port_value;
    char *end = NULL;
    const char *message;
    int result = 1;

    if (argc != 4) {
        fprintf(stderr, "usage: %s HOST PORT MESSAGE\n", argv[0]);
        return 2;
    }
    port_value = strtoul(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || port_value == 0ul
        || port_value > 65535ul) {
        fprintf(stderr, "invalid port: %s\n", argv[2]);
        return 2;
    }
    message = argv[3];
    if (draw_online_client_connect(
            &client,
            argv[1],
            (uint16_t)port_value,
            3000)
            != 0
        || draw_online_client_login(client, 3000) != 0
        || draw_online_client_join(client, 1u, &player_slot, 3000) != 0
        || draw_online_client_send_game(
               client,
               1u,
               message,
               strlen(message),
               3000)
            != 0
        || draw_online_client_receive(client, &event, 3000) != 0) {
        fprintf(stderr, "demo exchange failed\n");
        goto done;
    }
    if (event.header.route != DRAW_WIRE_ROUTE_GAME
        || event.header.kind != DRAW_WIRE_GAME_DATA
        || event.header.room_id != 1u
        || event.header.player_slot != player_slot
        || event.payload_length != strlen(message)
        || memcmp(event.payload, message, event.payload_length) != 0) {
        fprintf(stderr, "unexpected echo response\n");
        draw_online_client_event_release(&event);
        goto done;
    }
    printf(
        "UID=%llu SLOT=%u ECHO=%.*s\n",
        (unsigned long long)draw_online_client_uid(client),
        (unsigned)player_slot,
        (int)event.payload_length,
        (const char *)event.payload);
    draw_online_client_event_release(&event);
    result = 0;

done:
    draw_online_client_close(client);
    return result;
}
