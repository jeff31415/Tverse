#include "draw_online/room_request.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t *draw_room_request_inflight_for_origin(
    DrawRoomRequestQueue *queue,
    DrawRoomRequestOrigin origin)
{
    if (origin == DRAW_ROOM_REQUEST_ORIGIN_LOBBY) {
        return &queue->lobby_inflight;
    }
    if (origin == DRAW_ROOM_REQUEST_ORIGIN_MAIN) {
        return &queue->main_inflight;
    }
    return NULL;
}

static size_t draw_room_request_limit_for_origin(
    const DrawRoomRequestQueue *queue,
    DrawRoomRequestOrigin origin)
{
    if (origin == DRAW_ROOM_REQUEST_ORIGIN_LOBBY) {
        return queue->lobby_inflight_limit;
    }
    if (origin == DRAW_ROOM_REQUEST_ORIGIN_MAIN) {
        return queue->main_inflight_limit;
    }
    return 0u;
}

int draw_room_request_queue_init(
    DrawRoomRequestQueue *queue,
    size_t record_capacity,
    size_t lobby_inflight_limit,
    size_t main_inflight_limit)
{
    size_t credit_capacity;
    int status;

    if (queue == NULL || record_capacity == 0u || lobby_inflight_limit == 0u
        || main_inflight_limit == 0u
        || lobby_inflight_limit > SIZE_MAX - main_inflight_limit) {
        return DRAW_QUEUE_INVALID;
    }
    credit_capacity = lobby_inflight_limit + main_inflight_limit;
    if (credit_capacity > SIZE_MAX / sizeof(*queue->credits)
        || record_capacity > SIZE_MAX / sizeof(DrawRoomRequest)) {
        return DRAW_QUEUE_INVALID;
    }

    memset(queue, 0, sizeof(*queue));
    queue->credits = calloc(credit_capacity, sizeof(*queue->credits));
    if (queue->credits == NULL) {
        return DRAW_QUEUE_SYSTEM_ERROR;
    }
    status = draw_owned_queue_init(
        &queue->queue,
        record_capacity,
        record_capacity * sizeof(DrawRoomRequest),
        0u,
        0u);
    if (status != DRAW_QUEUE_OK) {
        free(queue->credits);
        memset(queue, 0, sizeof(*queue));
        return status;
    }
    queue->credit_capacity = credit_capacity;
    queue->lobby_inflight_limit = lobby_inflight_limit;
    queue->main_inflight_limit = main_inflight_limit;
    return DRAW_QUEUE_OK;
}

int draw_room_request_submit(
    DrawRoomRequestQueue *queue,
    DrawRoomRequest *request)
{
    size_t *inflight;
    size_t limit;
    size_t empty_credit = SIZE_MAX;
    size_t index;
    DrawOwnedRecord record;
    int status = DRAW_QUEUE_OK;

    if (queue == NULL || request == NULL || request->request_id == 0u) {
        return DRAW_QUEUE_INVALID;
    }
    inflight = draw_room_request_inflight_for_origin(queue, request->origin);
    limit = draw_room_request_limit_for_origin(queue, request->origin);
    if (inflight == NULL || limit == 0u) {
        return DRAW_QUEUE_INVALID;
    }

    record.item = request;
    record.bytes = sizeof(*request);
    record.control = false;
    if (pthread_mutex_lock(&queue->queue.mutex) != 0) {
        return DRAW_QUEUE_SYSTEM_ERROR;
    }
    for (index = 0u; index < queue->credit_capacity; ++index) {
        DrawRoomInflightCredit *credit = &queue->credits[index];
        if (!credit->occupied && empty_credit == SIZE_MAX) {
            empty_credit = index;
        } else if (credit->occupied && credit->origin == request->origin
                   && credit->request_id == request->request_id) {
            status = DRAW_QUEUE_INVALID;
            goto done;
        }
    }
    if (queue->queue.closed) {
        status = DRAW_QUEUE_CLOSED;
        goto done;
    }
    if (*inflight >= limit || queue->queue.record_count >= queue->queue.record_capacity
        || record.bytes > queue->queue.byte_capacity - queue->queue.bytes_used) {
        status = DRAW_QUEUE_FULL;
        goto done;
    }
    if (empty_credit == SIZE_MAX) {
        status = DRAW_QUEUE_FULL;
        goto done;
    }

    queue->credits[empty_credit].origin = request->origin;
    queue->credits[empty_credit].request_id = request->request_id;
    queue->credits[empty_credit].occupied = true;
    *inflight += 1u;

    queue->queue.records[queue->queue.tail] = record;
    queue->queue.tail = (queue->queue.tail + 1u) % queue->queue.record_capacity;
    queue->queue.record_count += 1u;
    queue->queue.bytes_used += record.bytes;
    (void)pthread_cond_signal(&queue->queue.not_empty);

done:
    (void)pthread_mutex_unlock(&queue->queue.mutex);
    return status;
}

int draw_room_request_try_pop(
    DrawRoomRequestQueue *queue,
    DrawRoomRequest **out_request)
{
    DrawOwnedRecord record;
    int status;

    if (queue == NULL || out_request == NULL) {
        return DRAW_QUEUE_INVALID;
    }
    status = draw_owned_queue_try_pop(&queue->queue, &record);
    if (status != DRAW_QUEUE_OK) {
        return status;
    }
    *out_request = record.item;
    return DRAW_QUEUE_OK;
}

int draw_room_request_complete_after_result_pop(
    DrawRoomRequestQueue *queue,
    DrawRoomRequestOrigin origin,
    uint64_t request_id)
{
    size_t *inflight;
    size_t index;
    int status = DRAW_QUEUE_INVALID;

    if (queue == NULL || request_id == 0u) {
        return DRAW_QUEUE_INVALID;
    }
    inflight = draw_room_request_inflight_for_origin(queue, origin);
    if (inflight == NULL) {
        return DRAW_QUEUE_INVALID;
    }
    if (pthread_mutex_lock(&queue->queue.mutex) != 0) {
        return DRAW_QUEUE_SYSTEM_ERROR;
    }
    for (index = 0u; index < queue->credit_capacity; ++index) {
        DrawRoomInflightCredit *credit = &queue->credits[index];
        if (credit->occupied && credit->origin == origin
            && credit->request_id == request_id) {
            memset(credit, 0, sizeof(*credit));
            if (*inflight != 0u) {
                *inflight -= 1u;
                status = DRAW_QUEUE_OK;
            }
            break;
        }
    }
    (void)pthread_cond_signal(&queue->queue.not_full);
    (void)pthread_mutex_unlock(&queue->queue.mutex);
    return status;
}

void draw_room_request_queue_close(DrawRoomRequestQueue *queue)
{
    if (queue != NULL) {
        draw_owned_queue_close(&queue->queue);
    }
}

void draw_room_request_queue_destroy(DrawRoomRequestQueue *queue)
{
    if (queue == NULL) {
        return;
    }
    draw_owned_queue_destroy(&queue->queue, free);
    free(queue->credits);
    memset(queue, 0, sizeof(*queue));
}
