#include "draw_online/room_plugin.h"

#include <string.h>

DRAW_ROOM_EXPORT int server_room_entry(DrawRoomContext *context)
{
    if (context == NULL || context->abi_version != DRAW_ROOM_ABI_VERSION
        || context->api.wait == NULL || context->api.read == NULL
        || context->api.write == NULL || context->api.stop_requested == NULL
        || context->api.mark_ready == NULL) {
        return DRAW_ROOM_INVALID;
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
            memset(&output, 0, sizeof(output));
            output.recipients = DRAW_ROOM_RECIPIENT_ONE;
            output.player_slot = input.player_slot;
            output.payload = input.payload;
            output.payload_length = input.payload_length;
            if (context->api.write(context->userdata, &output) != DRAW_ROOM_OK
                && !context->api.stop_requested(context->userdata)) {
                return DRAW_ROOM_ERROR;
            }
        }
    }
    return DRAW_ROOM_OK;
}
