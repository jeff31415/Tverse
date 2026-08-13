/*
 * Proves that a room entry which returns while the server is still running is
 * detected, that its members are evicted with an explicit code rather than
 * being left to time out, and that the room is replaced by a fresh generation
 * which serves traffic normally.
 *
 * Before draw_server_supervise_room existed, the room's exit was invisible:
 * room_finished was only ever read during startup, so the inbox filled and
 * every subsequent client saw BUSY with no indication of why.
 */
#include "draw_online/client.h"
#include "draw_online/server.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK_GOTO(condition)                                                  \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                     \
                __FILE__, __LINE__, #condition);                               \
            result = 1;                                                        \
            goto done;                                                         \
        }                                                                      \
    } while (0)

/*
 * Reads until a GAME_RESULT arrives, tolerating any GAME_DATA still queued
 * ahead of it. Returns 0 and fills out_code on success.
 */
static int await_game_result(
    DrawOnlineClient *client,
    uint32_t *out_code,
    int attempts)
{
    int attempt;

    for (attempt = 0; attempt < attempts; ++attempt) {
        DrawOnlineClientEvent event;

        memset(&event, 0, sizeof(event));
        if (draw_online_client_receive(client, &event, 3000) != 0) {
            return -1;
        }
        if (event.header.route == DRAW_WIRE_ROUTE_GAME
            && event.header.kind == DRAW_WIRE_GAME_RESULT) {
            *out_code = event.header.code;
            draw_online_client_event_release(&event);
            return 0;
        }
        draw_online_client_event_release(&event);
    }
    return -1;
}

static int probe_generation(
    DrawOnlineClient *client,
    uint32_t room_id,
    uint8_t *out_generation)
{
    static const uint8_t probe[] = "__generation__";
    DrawOnlineClientEvent event;
    int status = -1;

    memset(&event, 0, sizeof(event));
    if (draw_online_client_send_game(
            client,
            room_id,
            probe,
            sizeof(probe) - 1u,
            3000)
        != 0) {
        return -1;
    }
    if (draw_online_client_receive(client, &event, 3000) != 0) {
        return -1;
    }
    if (event.header.kind == DRAW_WIRE_GAME_DATA && event.payload_length == 1u) {
        *out_generation = ((const uint8_t *)event.payload)[0];
        status = 0;
    }
    draw_online_client_event_release(&event);
    return status;
}

int main(void)
{
    DrawOnlineServerOptions options;
    DrawOnlineServer *server = NULL;
    DrawOnlineClient *client = NULL;
    static const uint8_t exit_probe[] = "__exit__";
    uint32_t first_slot = 0u;
    uint32_t rejoined_slot = 0u;
    uint32_t eviction_code = DRAW_WIRE_CODE_OK;
    uint8_t generation_before = 0u;
    uint8_t generation_after = 0u;
    uint16_t port;
    int result = 0;

    memset(&options, 0, sizeof(options));
    options.bind_host = "127.0.0.1";
    options.port = 0u;
    options.room_module_path = DRAW_ONLINE_TEST_EXITING_ROOM_MODULE;
    CHECK_GOTO(draw_online_server_start(&server, &options) == 0);
    port = draw_online_server_port(server);
    CHECK_GOTO(port != 0u);

    CHECK_GOTO(draw_online_client_connect(&client, "127.0.0.1", port, 3000) == 0);
    CHECK_GOTO(draw_online_client_login(client, 3000) == 0);
    CHECK_GOTO(draw_online_client_join(client, 1u, &first_slot, 3000) == 0);
    CHECK_GOTO(first_slot != 0u);

    /* The first generation is serving normally. */
    CHECK_GOTO(probe_generation(client, 1u, &generation_before) == 0);
    CHECK_GOTO(generation_before == 1u);

    /* Kill the room mid-life. */
    CHECK_GOTO(draw_online_client_send_game(
                   client,
                   1u,
                   exit_probe,
                   sizeof(exit_probe) - 1u,
                   3000)
        == 0);

    /* The member is evicted with an explicit internal-failure code, not left
     * hanging. Supervision runs on the Gateway's 100 ms tick. */
    CHECK_GOTO(await_game_result(client, &eviction_code, 4) == 0);
    CHECK_GOTO(eviction_code == DRAW_WIRE_CODE_INTERNAL);

    /* The identity survived the room's death, so the same client can rejoin,
     * and the replacement room is a genuinely new generation. */
    CHECK_GOTO(draw_online_client_join(client, 1u, &rejoined_slot, 3000) == 0);
    CHECK_GOTO(rejoined_slot != 0u);
    CHECK_GOTO(probe_generation(client, 1u, &generation_after) == 0);
    CHECK_GOTO(generation_after == 2u);

done:
    draw_online_client_close(client);
    draw_online_server_destroy(server);
    return result;
}
