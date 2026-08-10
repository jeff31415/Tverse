#ifndef DRAW_ONLINE_ROOM_PLUGIN_H
#define DRAW_ONLINE_ROOM_PLUGIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DRAW_ROOM_ABI_VERSION 1u
#define DRAW_ROOM_ENTRY_SYMBOL "server_room_entry"

#if defined(_WIN32)
#define DRAW_ROOM_EXPORT __declspec(dllexport)
#else
#define DRAW_ROOM_EXPORT __attribute__((visibility("default")))
#endif

typedef enum DrawRoomStatus {
    DRAW_ROOM_OK = 0,
    DRAW_ROOM_EMPTY = 1,
    DRAW_ROOM_FULL = 2,
    DRAW_ROOM_CLOSED = 3,
    DRAW_ROOM_STOPPED = 4,
    DRAW_ROOM_INVALID = -1,
    DRAW_ROOM_ERROR = -2
} DrawRoomStatus;

typedef enum DrawRoomRecordKind {
    DRAW_ROOM_RECORD_DATA = 1,
    DRAW_ROOM_RECORD_PLAYER_JOIN = 2,
    DRAW_ROOM_RECORD_CONNECTION_LOST = 3,
    DRAW_ROOM_RECORD_CONNECTION_RESUMED = 4,
    DRAW_ROOM_RECORD_PLAYER_REMOVED = 5,
    DRAW_ROOM_RECORD_STOP = 6
} DrawRoomRecordKind;

typedef struct DrawRoomInput {
    DrawRoomRecordKind kind;
    uint32_t player_slot;
    uint32_t code;
    const uint8_t *payload;
    size_t payload_length;
} DrawRoomInput;

typedef enum DrawRoomRecipientKind {
    DRAW_ROOM_RECIPIENT_ONE = 1,
    DRAW_ROOM_RECIPIENT_ALL = 2
} DrawRoomRecipientKind;

typedef struct DrawRoomOutput {
    DrawRoomRecipientKind recipients;
    uint32_t player_slot;
    const uint8_t *payload;
    size_t payload_length;
} DrawRoomOutput;

typedef struct DrawRoomApi {
    int (*wait)(void *userdata, int timeout_ms);
    int (*read)(void *userdata, DrawRoomInput *out_input);
    int (*write)(void *userdata, const DrawRoomOutput *output);
    bool (*stop_requested)(void *userdata);
    int (*mark_ready)(void *userdata);
    int64_t (*monotonic_now_ns)(void *userdata);
} DrawRoomApi;

typedef struct DrawRoomContext {
    uint32_t abi_version;
    uint64_t room_id;
    const uint8_t *config;
    size_t config_length;
    void *userdata;
    DrawRoomApi api;
} DrawRoomContext;

typedef int (*DrawRoomEntryFn)(DrawRoomContext *context);

DRAW_ROOM_EXPORT int server_room_entry(DrawRoomContext *context);

#ifdef __cplusplus
}
#endif

#endif
