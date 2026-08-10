#include "draw_online/owned_queue.h"
#include "draw_online/room_request.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                __FILE__, __LINE__, #condition);                               \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static DrawOwnedRecord make_record(size_t bytes, int control)
{
    DrawOwnedRecord record;

    record.item = malloc(1u);
    record.bytes = bytes;
    record.control = control != 0;
    return record;
}

static int test_owned_queue_reserve(void)
{
    DrawOwnedQueue queue;
    DrawOwnedRecord records[5];
    DrawOwnedRecord popped;
    DrawOwnedRecord oversized;
    size_t index;

    CHECK(draw_owned_queue_init(&queue, 4u, 100u, 1u, 20u) == DRAW_QUEUE_OK);
    for (index = 0u; index < 3u; ++index) {
        records[index] = make_record(20u, 0);
        CHECK(records[index].item != NULL);
        CHECK(draw_owned_queue_try_push(&queue, &records[index]) == DRAW_QUEUE_OK);
        CHECK(records[index].item == NULL);
    }
    records[3] = make_record(1u, 0);
    CHECK(draw_owned_queue_try_push(&queue, &records[3]) == DRAW_QUEUE_FULL);
    CHECK(records[3].item != NULL);

    records[4] = make_record(20u, 1);
    CHECK(draw_owned_queue_try_push(&queue, &records[4]) == DRAW_QUEUE_OK);
    CHECK(records[4].item == NULL);
    CHECK(draw_owned_queue_try_pop(&queue, &popped) == DRAW_QUEUE_OK);
    free(popped.item);

    oversized = make_record(81u, 0);
    CHECK(draw_owned_queue_push_wait(&queue, &oversized, NULL, 0)
        == DRAW_QUEUE_FULL);
    free(oversized.item);

    draw_owned_queue_close(&queue);
    CHECK(draw_owned_queue_try_push(&queue, &records[3]) == DRAW_QUEUE_CLOSED);
    free(records[3].item);
    draw_owned_queue_destroy(&queue, free);
    return 0;
}

static DrawRoomRequest *make_request(uint64_t request_id)
{
    DrawRoomRequest *request = calloc(1u, sizeof(*request));

    if (request != NULL) {
        request->origin = DRAW_ROOM_REQUEST_ORIGIN_LOBBY;
        request->kind = DRAW_ROOM_REQUEST_JOIN_UID;
        request->request_id = request_id;
        request->uid = request_id + 100u;
        request->room_id = 1u;
    }
    return request;
}

static int test_room_request_credit_lifetime(void)
{
    DrawRoomRequestQueue queue;
    DrawRoomRequest *first = make_request(1u);
    DrawRoomRequest *second = make_request(2u);
    DrawRoomRequest *third = make_request(3u);
    DrawRoomRequest *duplicate = make_request(2u);
    DrawRoomRequest *popped = NULL;

    CHECK(first != NULL && second != NULL && third != NULL && duplicate != NULL);
    CHECK(draw_room_request_queue_init(&queue, 4u, 2u, 1u) == DRAW_QUEUE_OK);
    CHECK(draw_room_request_submit(&queue, first) == DRAW_QUEUE_OK);
    CHECK(draw_room_request_submit(&queue, second) == DRAW_QUEUE_OK);
    CHECK(draw_room_request_submit(&queue, duplicate) == DRAW_QUEUE_INVALID);
    free(duplicate);
    CHECK(draw_room_request_submit(&queue, third) == DRAW_QUEUE_FULL);
    CHECK(draw_room_request_try_pop(&queue, &popped) == DRAW_QUEUE_OK);
    CHECK(popped == first);
    free(popped);

    CHECK(draw_room_request_submit(&queue, third) == DRAW_QUEUE_FULL);
    CHECK(draw_room_request_complete_after_result_pop(
              &queue,
              DRAW_ROOM_REQUEST_ORIGIN_LOBBY,
              999u)
        == DRAW_QUEUE_INVALID);
    CHECK(draw_room_request_complete_after_result_pop(
              &queue,
              DRAW_ROOM_REQUEST_ORIGIN_LOBBY,
              1u)
        == DRAW_QUEUE_OK);
    CHECK(draw_room_request_complete_after_result_pop(
              &queue,
              DRAW_ROOM_REQUEST_ORIGIN_LOBBY,
              1u)
        == DRAW_QUEUE_INVALID);
    CHECK(draw_room_request_submit(&queue, third) == DRAW_QUEUE_OK);

    while (draw_room_request_try_pop(&queue, &popped) == DRAW_QUEUE_OK) {
        (void)draw_room_request_complete_after_result_pop(
            &queue,
            popped->origin,
            popped->request_id);
        free(popped);
    }
    draw_room_request_queue_close(&queue);
    draw_room_request_queue_destroy(&queue);
    return 0;
}

#define STRESS_PRODUCERS 4u
#define STRESS_REQUESTS_PER_PRODUCER 200u

typedef struct StressProducerContext {
    DrawRoomRequestQueue *queue;
    size_t producer_index;
    _Atomic int *failed;
} StressProducerContext;

static void *stress_producer_main(void *userdata)
{
    StressProducerContext *context = userdata;
    const DrawRoomRequestOrigin origin = (context->producer_index % 2u) == 0u
        ? DRAW_ROOM_REQUEST_ORIGIN_LOBBY
        : DRAW_ROOM_REQUEST_ORIGIN_MAIN;
    const uint64_t producer_within_origin =
        (uint64_t)(context->producer_index / 2u);
    size_t index;

    for (index = 0u; index < STRESS_REQUESTS_PER_PRODUCER; ++index) {
        DrawRoomRequest *request = calloc(1u, sizeof(*request));
        int status;

        if (request == NULL) {
            atomic_store(context->failed, 1);
            return NULL;
        }
        request->origin = origin;
        request->kind = DRAW_ROOM_REQUEST_JOIN_UID;
        request->request_id = producer_within_origin
                * (uint64_t)STRESS_REQUESTS_PER_PRODUCER
            + (uint64_t)index + 1u;
        request->uid = request->request_id + 1000u;
        request->room_id = 1u;
        do {
            status = draw_room_request_submit(context->queue, request);
            if (status == DRAW_QUEUE_FULL) {
                (void)sched_yield();
            }
        } while (status == DRAW_QUEUE_FULL
                 && atomic_load(context->failed) == 0);
        if (status != DRAW_QUEUE_OK) {
            free(request);
            atomic_store(context->failed, 1);
            return NULL;
        }
    }
    return NULL;
}

static int test_room_request_mpsc_stress(void)
{
    DrawRoomRequestQueue queue;
    pthread_t producers[STRESS_PRODUCERS];
    StressProducerContext contexts[STRESS_PRODUCERS];
    bool seen[2][STRESS_REQUESTS_PER_PRODUCER * 2u + 1u] = {{false}};
    _Atomic int failed;
    size_t started = 0u;
    size_t consumed = 0u;
    size_t index;

    atomic_init(&failed, 0);
    CHECK(draw_room_request_queue_init(&queue, 32u, 16u, 16u)
        == DRAW_QUEUE_OK);
    for (index = 0u; index < STRESS_PRODUCERS; ++index) {
        contexts[index].queue = &queue;
        contexts[index].producer_index = index;
        contexts[index].failed = &failed;
        if (pthread_create(
                &producers[index],
                NULL,
                stress_producer_main,
                &contexts[index])
            != 0) {
            atomic_store(&failed, 1);
            break;
        }
        started += 1u;
    }

    while (consumed < STRESS_PRODUCERS * STRESS_REQUESTS_PER_PRODUCER
           && atomic_load(&failed) == 0) {
        DrawRoomRequest *request = NULL;
        const int status = draw_room_request_try_pop(&queue, &request);

        if (status == DRAW_QUEUE_EMPTY) {
            (void)sched_yield();
            continue;
        }
        if (status != DRAW_QUEUE_OK || request == NULL
            || request->request_id > STRESS_REQUESTS_PER_PRODUCER * 2u) {
            atomic_store(&failed, 1);
            break;
        }
        index = request->origin == DRAW_ROOM_REQUEST_ORIGIN_LOBBY ? 0u : 1u;
        if (seen[index][request->request_id]) {
            atomic_store(&failed, 1);
            free(request);
            break;
        }
        seen[index][request->request_id] = true;
        if (draw_room_request_complete_after_result_pop(
                &queue,
                request->origin,
                request->request_id)
            != DRAW_QUEUE_OK) {
            atomic_store(&failed, 1);
            free(request);
            break;
        }
        free(request);
        consumed += 1u;
    }
    if (atomic_load(&failed) != 0) {
        draw_room_request_queue_close(&queue);
    }
    for (index = 0u; index < started; ++index) {
        (void)pthread_join(producers[index], NULL);
    }
    CHECK(atomic_load(&failed) == 0);
    CHECK(consumed == STRESS_PRODUCERS * STRESS_REQUESTS_PER_PRODUCER);
    CHECK(queue.lobby_inflight == 0u);
    CHECK(queue.main_inflight == 0u);
    draw_room_request_queue_close(&queue);
    draw_room_request_queue_destroy(&queue);
    return 0;
}

int main(void)
{
    if (test_owned_queue_reserve() != 0) {
        return 1;
    }
    if (test_room_request_credit_lifetime() != 0) {
        return 1;
    }
    return test_room_request_mpsc_stress();
}
