#ifndef DRAW_ONLINE_WIRE_H
#define DRAW_ONLINE_WIRE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DRAW_WIRE_HEADER_SIZE 64u
#define DRAW_WIRE_LENGTH_PREFIX_SIZE 4u
#define DRAW_WIRE_VERSION 1u
#define DRAW_WIRE_MAX_PAYLOAD 65536u

typedef enum DrawWireStatus {
    DRAW_WIRE_OK = 0,
    DRAW_WIRE_INVALID = -1,
    DRAW_WIRE_OVERFLOW = -2,
    DRAW_WIRE_UNSUPPORTED = -3
} DrawWireStatus;

typedef enum DrawWireRoute {
    DRAW_WIRE_ROUTE_AUTH = 1,
    DRAW_WIRE_ROUTE_LOBBY = 2,
    DRAW_WIRE_ROUTE_GAME = 3
} DrawWireRoute;

typedef enum DrawWireAuthKind {
    DRAW_WIRE_AUTH_LOGIN = 1,
    DRAW_WIRE_AUTH_RESUME = 2,
    DRAW_WIRE_AUTH_LOGOUT = 3,
    DRAW_WIRE_AUTH_RESULT = 4
} DrawWireAuthKind;

typedef enum DrawWireLobbyKind {
    DRAW_WIRE_LOBBY_LIST = 1,
    DRAW_WIRE_LOBBY_CREATE = 2,
    DRAW_WIRE_LOBBY_JOIN = 3,
    DRAW_WIRE_LOBBY_LEAVE = 4,
    DRAW_WIRE_LOBBY_RESULT = 5
} DrawWireLobbyKind;

typedef enum DrawWireGameKind {
    DRAW_WIRE_GAME_DATA = 1,
    DRAW_WIRE_GAME_RESULT = 2
} DrawWireGameKind;

typedef enum DrawWireCode {
    DRAW_WIRE_CODE_OK = 0,
    DRAW_WIRE_CODE_INVALID = 1,
    DRAW_WIRE_CODE_UNAUTHORIZED = 2,
    DRAW_WIRE_CODE_NOT_FOUND = 3,
    DRAW_WIRE_CODE_BUSY = 4,
    DRAW_WIRE_CODE_INTERNAL = 5
} DrawWireCode;

typedef struct DrawWireHeader {
    uint16_t flags;
    uint16_t route;
    uint16_t kind;
    uint32_t payload_length;
    uint64_t uid;
    uint8_t identity_handle[16];
    uint64_t room_id;
    uint64_t sequence;
    uint32_t player_slot;
    uint32_t code;
} DrawWireHeader;

int draw_wire_header_encode(
    uint8_t wire[DRAW_WIRE_HEADER_SIZE],
    const DrawWireHeader *header);

int draw_wire_header_decode(
    const uint8_t wire[DRAW_WIRE_HEADER_SIZE],
    DrawWireHeader *out_header);

int draw_wire_frame_length(
    const DrawWireHeader *header,
    uint32_t *out_frame_length);

void draw_wire_u32_encode(uint8_t out[4], uint32_t value);
uint32_t draw_wire_u32_decode(const uint8_t in[4]);

#ifdef __cplusplus
}
#endif

#endif
