#include "draw_online/client.h"
#include "draw_online/server.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK_GOTO(condition)                                                  \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                __FILE__, __LINE__, #condition);                               \
            result = 1;                                                        \
            goto done;                                                         \
        }                                                                      \
    } while (0)

int main(void)
{
    DrawOnlineServerOptions options;
    DrawOnlineServer *server = NULL;
    DrawOnlineClient *first = NULL;
    DrawOnlineClient *second = NULL;
    DrawOnlineClientEvent event;
    const uint8_t first_payload[] = {0x00u, 0x10u, 0x20u, 0xffu, 0x7fu};
    const uint8_t second_payload[] = "second-client";
    uint32_t first_slot = 0u;
    uint32_t repeated_slot = 0u;
    uint32_t rejected_slot = 0u;
    uint32_t second_slot = 0u;
    uint16_t port;
    int result = 0;

    memset(&event, 0, sizeof(event));
    memset(&options, 0, sizeof(options));
    options.bind_host = "127.0.0.1";
    options.port = 0u;
    options.room_module_path = DRAW_ONLINE_TEST_ROOM_MODULE;
    CHECK_GOTO(draw_online_server_start(&server, &options) == 0);
    port = draw_online_server_port(server);
    CHECK_GOTO(port != 0u);

    CHECK_GOTO(draw_online_client_connect(
                   &first,
                   "127.0.0.1",
                   port,
                   3000)
        == 0);
    CHECK_GOTO(draw_online_client_login(first, 3000) == 0);
    CHECK_GOTO(draw_online_client_uid(first) != 0u);
    CHECK_GOTO(draw_online_client_join(first, 999u, &rejected_slot, 3000) != 0);
    CHECK_GOTO(draw_online_client_join(first, 1u, &first_slot, 3000) == 0);
    CHECK_GOTO(first_slot != 0u);
    CHECK_GOTO(draw_online_client_join(first, 1u, &repeated_slot, 3000) == 0);
    CHECK_GOTO(repeated_slot == first_slot);
    CHECK_GOTO(draw_online_client_send_game(
                   first,
                   1u,
                   first_payload,
                   sizeof(first_payload),
                   3000)
        == 0);
    CHECK_GOTO(draw_online_client_receive(first, &event, 3000) == 0);
    CHECK_GOTO(event.header.route == DRAW_WIRE_ROUTE_GAME);
    CHECK_GOTO(event.header.kind == DRAW_WIRE_GAME_DATA);
    CHECK_GOTO(event.header.room_id == 1u);
    CHECK_GOTO(event.header.player_slot == first_slot);
    CHECK_GOTO(event.payload_length == sizeof(first_payload));
    CHECK_GOTO(memcmp(event.payload, first_payload, sizeof(first_payload)) == 0);
    draw_online_client_event_release(&event);

    CHECK_GOTO(draw_online_client_connect(
                   &second,
                   "127.0.0.1",
                   port,
                   3000)
        == 0);
    CHECK_GOTO(draw_online_client_login(second, 3000) == 0);
    CHECK_GOTO(draw_online_client_uid(second) != draw_online_client_uid(first));
    CHECK_GOTO(draw_online_client_join(second, 1u, &second_slot, 3000) == 0);
    CHECK_GOTO(second_slot != 0u && second_slot != first_slot);
    CHECK_GOTO(draw_online_client_send_game(
                   second,
                   1u,
                   second_payload,
                   sizeof(second_payload) - 1u,
                   3000)
        == 0);
    CHECK_GOTO(draw_online_client_receive(second, &event, 3000) == 0);
    CHECK_GOTO(event.header.player_slot == second_slot);
    CHECK_GOTO(event.payload_length == sizeof(second_payload) - 1u);
    CHECK_GOTO(memcmp(
                   event.payload,
                   second_payload,
                   sizeof(second_payload) - 1u)
        == 0);
    draw_online_client_event_release(&event);

done:
    draw_online_client_event_release(&event);
    draw_online_client_close(second);
    draw_online_client_close(first);
    draw_online_server_destroy(server);
    return result;
}
