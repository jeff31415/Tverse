#include "draw_online/wire.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                __FILE__, __LINE__, #condition);                               \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(void)
{
    DrawWireHeader header;
    DrawWireHeader decoded;
    uint8_t wire[DRAW_WIRE_HEADER_SIZE];
    uint32_t frame_length = 0u;
    size_t index;

    memset(&header, 0, sizeof(header));
    header.flags = UINT16_C(0x1020);
    header.route = DRAW_WIRE_ROUTE_GAME;
    header.kind = DRAW_WIRE_GAME_DATA;
    header.payload_length = UINT32_C(0x00000304);
    header.uid = UINT64_C(0x0102030405060708);
    for (index = 0u; index < sizeof(header.identity_handle); ++index) {
        header.identity_handle[index] = (uint8_t)(0xa0u + index);
    }
    header.room_id = UINT64_C(0x1112131415161718);
    header.sequence = UINT64_C(0x2122232425262728);
    header.player_slot = UINT32_C(0x31323334);
    header.code = UINT32_C(0x41424344);

    CHECK(draw_wire_header_encode(wire, &header) == DRAW_WIRE_OK);
    CHECK(memcmp(wire, "DG01", 4u) == 0);
    CHECK(wire[4] == 0u && wire[5] == 1u);
    CHECK(wire[12] == 0u && wire[13] == 0u && wire[14] == 3u
        && wire[15] == 4u);
    CHECK(wire[16] == 1u && wire[23] == 8u);
    CHECK(wire[24] == 0xa0u && wire[39] == 0xafu);
    CHECK(wire[40] == 0x11u && wire[47] == 0x18u);
    CHECK(wire[48] == 0x21u && wire[55] == 0x28u);
    CHECK(wire[56] == 0x31u && wire[59] == 0x34u);
    CHECK(wire[60] == 0x41u && wire[63] == 0x44u);

    CHECK(draw_wire_header_decode(wire, &decoded) == DRAW_WIRE_OK);
    CHECK(decoded.flags == header.flags);
    CHECK(decoded.route == header.route);
    CHECK(decoded.kind == header.kind);
    CHECK(decoded.payload_length == header.payload_length);
    CHECK(decoded.uid == header.uid);
    CHECK(memcmp(decoded.identity_handle, header.identity_handle, 16u) == 0);
    CHECK(decoded.room_id == header.room_id);
    CHECK(decoded.sequence == header.sequence);
    CHECK(decoded.player_slot == header.player_slot);
    CHECK(decoded.code == header.code);
    CHECK(draw_wire_frame_length(&header, &frame_length) == DRAW_WIRE_OK);
    CHECK(frame_length == DRAW_WIRE_HEADER_SIZE + header.payload_length);

    wire[0] = (uint8_t)'X';
    CHECK(draw_wire_header_decode(wire, &decoded) == DRAW_WIRE_INVALID);
    wire[0] = (uint8_t)'D';
    wire[5] = 2u;
    CHECK(draw_wire_header_decode(wire, &decoded) == DRAW_WIRE_UNSUPPORTED);

    memset(&header, 0, sizeof(header));
    header.route = DRAW_WIRE_ROUTE_GAME;
    header.kind = DRAW_WIRE_GAME_DATA;
    header.payload_length = DRAW_WIRE_MAX_PAYLOAD + 1u;
    CHECK(draw_wire_header_encode(wire, &header) == DRAW_WIRE_INVALID);
    return 0;
}
