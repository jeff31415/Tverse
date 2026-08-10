#include "draw_online/wire.h"

#include <string.h>

static uint16_t draw_wire_read_u16(const uint8_t *wire)
{
    return (uint16_t)(((uint16_t)wire[0] << 8u) | (uint16_t)wire[1]);
}

static uint64_t draw_wire_read_u64(const uint8_t *wire)
{
    uint64_t value = 0u;
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        value = (value << 8u) | (uint64_t)wire[index];
    }
    return value;
}

static void draw_wire_write_u16(uint8_t *wire, uint16_t value)
{
    wire[0] = (uint8_t)(value >> 8u);
    wire[1] = (uint8_t)value;
}

static void draw_wire_write_u64(uint8_t *wire, uint64_t value)
{
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        const size_t shift = (7u - index) * 8u;
        wire[index] = (uint8_t)(value >> shift);
    }
}

void draw_wire_u32_encode(uint8_t out[4], uint32_t value)
{
    out[0] = (uint8_t)(value >> 24u);
    out[1] = (uint8_t)(value >> 16u);
    out[2] = (uint8_t)(value >> 8u);
    out[3] = (uint8_t)value;
}

uint32_t draw_wire_u32_decode(const uint8_t in[4])
{
    return ((uint32_t)in[0] << 24u) | ((uint32_t)in[1] << 16u)
        | ((uint32_t)in[2] << 8u) | (uint32_t)in[3];
}

static int draw_wire_header_is_valid(const DrawWireHeader *header)
{
    if (header == NULL || header->payload_length > DRAW_WIRE_MAX_PAYLOAD) {
        return 0;
    }
    if (header->route < DRAW_WIRE_ROUTE_AUTH
        || header->route > DRAW_WIRE_ROUTE_GAME || header->kind == 0u) {
        return 0;
    }
    return 1;
}

int draw_wire_header_encode(
    uint8_t wire[DRAW_WIRE_HEADER_SIZE],
    const DrawWireHeader *header)
{
    if (wire == NULL || !draw_wire_header_is_valid(header)) {
        return DRAW_WIRE_INVALID;
    }

    memset(wire, 0, DRAW_WIRE_HEADER_SIZE);
    wire[0] = (uint8_t)'D';
    wire[1] = (uint8_t)'G';
    wire[2] = (uint8_t)'0';
    wire[3] = (uint8_t)'1';
    draw_wire_write_u16(wire + 4u, DRAW_WIRE_VERSION);
    draw_wire_write_u16(wire + 6u, header->flags);
    draw_wire_write_u16(wire + 8u, header->route);
    draw_wire_write_u16(wire + 10u, header->kind);
    draw_wire_u32_encode(wire + 12u, header->payload_length);
    draw_wire_write_u64(wire + 16u, header->uid);
    memcpy(wire + 24u, header->identity_handle, 16u);
    draw_wire_write_u64(wire + 40u, header->room_id);
    draw_wire_write_u64(wire + 48u, header->sequence);
    draw_wire_u32_encode(wire + 56u, header->player_slot);
    draw_wire_u32_encode(wire + 60u, header->code);
    return DRAW_WIRE_OK;
}

int draw_wire_header_decode(
    const uint8_t wire[DRAW_WIRE_HEADER_SIZE],
    DrawWireHeader *out_header)
{
    DrawWireHeader decoded;

    if (wire == NULL || out_header == NULL) {
        return DRAW_WIRE_INVALID;
    }
    if (wire[0] != (uint8_t)'D' || wire[1] != (uint8_t)'G'
        || wire[2] != (uint8_t)'0' || wire[3] != (uint8_t)'1') {
        return DRAW_WIRE_INVALID;
    }
    if (draw_wire_read_u16(wire + 4u) != DRAW_WIRE_VERSION) {
        return DRAW_WIRE_UNSUPPORTED;
    }

    memset(&decoded, 0, sizeof(decoded));
    decoded.flags = draw_wire_read_u16(wire + 6u);
    decoded.route = draw_wire_read_u16(wire + 8u);
    decoded.kind = draw_wire_read_u16(wire + 10u);
    decoded.payload_length = draw_wire_u32_decode(wire + 12u);
    decoded.uid = draw_wire_read_u64(wire + 16u);
    memcpy(decoded.identity_handle, wire + 24u, 16u);
    decoded.room_id = draw_wire_read_u64(wire + 40u);
    decoded.sequence = draw_wire_read_u64(wire + 48u);
    decoded.player_slot = draw_wire_u32_decode(wire + 56u);
    decoded.code = draw_wire_u32_decode(wire + 60u);

    if (!draw_wire_header_is_valid(&decoded)) {
        return DRAW_WIRE_INVALID;
    }
    *out_header = decoded;
    return DRAW_WIRE_OK;
}

int draw_wire_frame_length(
    const DrawWireHeader *header,
    uint32_t *out_frame_length)
{
    if (out_frame_length == NULL || !draw_wire_header_is_valid(header)) {
        return DRAW_WIRE_INVALID;
    }
    if (header->payload_length > UINT32_MAX - DRAW_WIRE_HEADER_SIZE) {
        return DRAW_WIRE_OVERFLOW;
    }
    *out_frame_length = DRAW_WIRE_HEADER_SIZE + header->payload_length;
    return DRAW_WIRE_OK;
}
