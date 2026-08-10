#ifndef DRAW_ONLINE_OWNED_QUEUE_H
#define DRAW_ONLINE_OWNED_QUEUE_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DrawQueueStatus {
    DRAW_QUEUE_OK = 0,
    DRAW_QUEUE_EMPTY = 1,
    DRAW_QUEUE_FULL = 2,
    DRAW_QUEUE_CLOSED = 3,
    DRAW_QUEUE_STOPPED = 4,
    DRAW_QUEUE_TIMED_OUT = 5,
    DRAW_QUEUE_INVALID = -1,
    DRAW_QUEUE_SYSTEM_ERROR = -2
} DrawQueueStatus;

typedef struct DrawOwnedRecord {
    void *item;
    size_t bytes;
    bool control;
} DrawOwnedRecord;

typedef struct DrawOwnedQueue {
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    DrawOwnedRecord *records;
    size_t record_capacity;
    size_t record_count;
    size_t head;
    size_t tail;
    size_t byte_capacity;
    size_t bytes_used;
    size_t reserved_control_records;
    size_t reserved_control_bytes;
    bool closed;
} DrawOwnedQueue;

typedef void (*DrawOwnedItemDestroyFn)(void *item);

int draw_owned_queue_init(
    DrawOwnedQueue *queue,
    size_t record_capacity,
    size_t byte_capacity,
    size_t reserved_control_records,
    size_t reserved_control_bytes);

int draw_owned_queue_try_push(
    DrawOwnedQueue *queue,
    DrawOwnedRecord *record);

int draw_owned_queue_push_wait(
    DrawOwnedQueue *queue,
    DrawOwnedRecord *record,
    const _Atomic bool *stop_requested,
    int timeout_ms);

int draw_owned_queue_try_pop(
    DrawOwnedQueue *queue,
    DrawOwnedRecord *out_record);

int draw_owned_queue_pop_wait(
    DrawOwnedQueue *queue,
    DrawOwnedRecord *out_record,
    const _Atomic bool *stop_requested,
    int timeout_ms);

void draw_owned_queue_wake_all(DrawOwnedQueue *queue);
void draw_owned_queue_close(DrawOwnedQueue *queue);
void draw_owned_queue_destroy(
    DrawOwnedQueue *queue,
    DrawOwnedItemDestroyFn destroy_item);

#ifdef __cplusplus
}
#endif

#endif
