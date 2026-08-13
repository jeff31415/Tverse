/*
 * Test room module. Behaves like the example echo room until it receives the
 * exit probe, at which point server_room_entry returns an error. It exists to
 * exercise draw_server_supervise_room: a room that dies while the server is
 * still running must be detected, its members evicted, and the room restarted.
 *
 * Each generation echoes its own generation number back for a "which
 * generation am I talking to" probe, so a test can prove the room was really
 * replaced rather than merely still alive.
 */
#include "draw_online/room_plugin.h"

#include <string.h>

#define DRAW_EXITING_ROOM_EXIT_PROBE "__exit__"
#define DRAW_EXITING_ROOM_GENERATION_PROBE "__generation__"

/*
 * Process-global on purpose: the host keeps the module loaded across restarts,
 * so this counts how many times the entry point has been invoked.
 */
static unsigned char draw_exiting_room_generation;

static int draw_exiting_room_matches(
    const DrawRoomInput *input,
    const char *probe,
    size_t probe_length)
{
    return input->payload != NULL && input->payload_length == probe_length
        && memcmp(input->payload, probe, probe_length) == 0;
}

DRAW_ROOM_EXPORT int server_room_entry(DrawRoomContext *context)
{
    if (context == NULL || context->abi_version != DRAW_ROOM_ABI_VERSION
        || context->api.wait == NULL || context->api.read == NULL
        || context->api.write == NULL || context->api.stop_requested == NULL
        || context->api.mark_ready == NULL) {
        return DRAW_ROOM_INVALID;
    }
    if (draw_exiting_room_generation < 255u) {
        draw_exiting_room_generation += 1u;
    }
    if (context->api.mark_ready(context->userdata) != DRAW_ROOM_OK) {
        return DRAW_ROOM_ERROR;
    }

    while (!context->api.stop_requested(context->userdata)) {
        DrawRoomInput input;
        int wait_status = context->api.wait(context->userdata, -1);

        if (wait_status == DRAW_ROOM_STOPPED) {
            break;
        }
        if (wait_status != DRAW_ROOM_OK && wait_status != DRAW_ROOM_EMPTY) {
            return DRAW_ROOM_ERROR;
        }
        while (context->api.read(context->userdata, &input) == DRAW_ROOM_OK) {
            DrawRoomOutput output;

            if (input.kind != DRAW_ROOM_RECORD_DATA) {
                continue;
            }
            if (draw_exiting_room_matches(
                    &input,
                    DRAW_EXITING_ROOM_EXIT_PROBE,
                    sizeof(DRAW_EXITING_ROOM_EXIT_PROBE) - 1u)) {
                /* Abandon the room mid-life, exactly as a fatal internal
                 * failure would. Nothing is written back. */
                return DRAW_ROOM_ERROR;
            }
            memset(&output, 0, sizeof(output));
            output.recipients = DRAW_ROOM_RECIPIENT_ONE;
            output.player_slot = input.player_slot;
            if (draw_exiting_room_matches(
                    &input,
                    DRAW_EXITING_ROOM_GENERATION_PROBE,
                    sizeof(DRAW_EXITING_ROOM_GENERATION_PROBE) - 1u)) {
                output.payload = &draw_exiting_room_generation;
                output.payload_length = 1u;
            } else {
                output.payload = input.payload;
                output.payload_length = input.payload_length;
            }
            if (context->api.write(context->userdata, &output) != DRAW_ROOM_OK
                && !context->api.stop_requested(context->userdata)) {
                return DRAW_ROOM_ERROR;
            }
        }
    }
    return DRAW_ROOM_OK;
}
