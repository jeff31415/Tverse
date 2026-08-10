#ifndef DRAW_ONLINE_ROOM_REQUEST_H
#define DRAW_ONLINE_ROOM_REQUEST_H

#include "draw_online/owned_queue.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DrawRoomRequestOrigin {
    DRAW_ROOM_REQUEST_ORIGIN_LOBBY = 1,
    DRAW_ROOM_REQUEST_ORIGIN_MAIN = 2
} DrawRoomRequestOrigin;

typedef enum DrawRoomRequestKind {
    DRAW_ROOM_REQUEST_JOIN_UID = 1,
    DRAW_ROOM_REQUEST_LEAVE_UID = 2,
    DRAW_ROOM_REQUEST_REMOVE_UID_ALL = 3,
    DRAW_ROOM_REQUEST_REMOVE_ALL_MEMBERS = 4,
    DRAW_ROOM_REQUEST_INSTALL_ROUTE = 5,
    DRAW_ROOM_REQUEST_REMOVE_ROUTE = 6,
    DRAW_ROOM_REQUEST_SET_TEMPLATE_ACTIVE = 7,
    DRAW_ROOM_REQUEST_SET_TEMPLATE_DRAINING = 8,
    DRAW_ROOM_REQUEST_SET_TEMPLATE_DISABLED = 9
} DrawRoomRequestKind;

typedef struct DrawConnectionRef {
    uint32_t slot;
    uint64_t generation;
} DrawConnectionRef;

typedef struct DrawRoomRequest {
    DrawRoomRequestOrigin origin;
    DrawRoomRequestKind kind;
    uint64_t request_id;
    uint64_t parent_request_id;
    uint64_t uid;
    uint64_t room_id;
    uint64_t room_template_id;
    uint64_t room_instance_generation;
    DrawConnectionRef connection;
    uint32_t player_slot;
    uint32_t flags;
    uint32_t code;
} DrawRoomRequest;

typedef struct DrawRoomInflightCredit {
    DrawRoomRequestOrigin origin;
    uint64_t request_id;
    bool occupied;
} DrawRoomInflightCredit;

typedef struct DrawRoomRequestQueue {
    DrawOwnedQueue queue;
    DrawRoomInflightCredit *credits;
    size_t credit_capacity;
    size_t lobby_inflight;
    size_t lobby_inflight_limit;
    size_t main_inflight;
    size_t main_inflight_limit;
} DrawRoomRequestQueue;

int draw_room_request_queue_init(
    DrawRoomRequestQueue *queue,
    size_t record_capacity,
    size_t lobby_inflight_limit,
    size_t main_inflight_limit);

int draw_room_request_submit(
    DrawRoomRequestQueue *queue,
    DrawRoomRequest *request);

int draw_room_request_try_pop(
    DrawRoomRequestQueue *queue,
    DrawRoomRequest **out_request);

int draw_room_request_complete_after_result_pop(
    DrawRoomRequestQueue *queue,
    DrawRoomRequestOrigin origin,
    uint64_t request_id);

void draw_room_request_queue_close(DrawRoomRequestQueue *queue);
void draw_room_request_queue_destroy(DrawRoomRequestQueue *queue);

#ifdef __cplusplus
}
#endif

#endif
