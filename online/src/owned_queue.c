#include "draw_online/owned_queue.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int draw_owned_queue_has_capacity_locked(
    const DrawOwnedQueue *queue,
    const DrawOwnedRecord *record)
{
    size_t record_limit;
    size_t byte_limit;

    if (record->bytes > queue->byte_capacity - queue->bytes_used) {
        return 0;
    }
    if (queue->record_count >= queue->record_capacity) {
        return 0;
    }
    if (record->control) {
        return 1;
    }

    record_limit = queue->record_capacity - queue->reserved_control_records;
    byte_limit = queue->byte_capacity - queue->reserved_control_bytes;
    if (queue->record_count >= record_limit || record->bytes > byte_limit) {
        return 0;
    }
    return queue->bytes_used <= byte_limit - record->bytes;
}

static void draw_owned_queue_add_timeout(
    struct timespec *deadline,
    int timeout_ms)
{
    const time_t seconds = (time_t)(timeout_ms / 1000);
    const long nanoseconds = (long)(timeout_ms % 1000) * 1000000L;

    deadline->tv_sec += seconds;
    deadline->tv_nsec += nanoseconds;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec += 1;
        deadline->tv_nsec -= 1000000000L;
    }
}

int draw_owned_queue_init(
    DrawOwnedQueue *queue,
    size_t record_capacity,
    size_t byte_capacity,
    size_t reserved_control_records,
    size_t reserved_control_bytes)
{
    pthread_condattr_t condition_attributes;
    int mutex_ready = 0;
    int condition_attributes_ready = 0;
    int not_empty_ready = 0;

    if (queue == NULL || record_capacity == 0u || byte_capacity == 0u
        || reserved_control_records > record_capacity
        || reserved_control_bytes > byte_capacity
        || record_capacity > SIZE_MAX / sizeof(*queue->records)) {
        return DRAW_QUEUE_INVALID;
    }

    memset(queue, 0, sizeof(*queue));
    queue->records = calloc(record_capacity, sizeof(*queue->records));
    if (queue->records == NULL) {
        return DRAW_QUEUE_SYSTEM_ERROR;
    }

    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        goto fail;
    }
    mutex_ready = 1;
    if (pthread_condattr_init(&condition_attributes) != 0) {
        goto fail;
    }
    condition_attributes_ready = 1;
    if (pthread_condattr_setclock(&condition_attributes, CLOCK_MONOTONIC) != 0) {
        goto fail;
    }
    if (pthread_cond_init(&queue->not_empty, &condition_attributes) != 0) {
        goto fail;
    }
    not_empty_ready = 1;
    if (pthread_cond_init(&queue->not_full, &condition_attributes) != 0) {
        goto fail;
    }
    (void)pthread_condattr_destroy(&condition_attributes);

    queue->record_capacity = record_capacity;
    queue->byte_capacity = byte_capacity;
    queue->reserved_control_records = reserved_control_records;
    queue->reserved_control_bytes = reserved_control_bytes;
    return DRAW_QUEUE_OK;

fail:
    if (not_empty_ready) {
        (void)pthread_cond_destroy(&queue->not_empty);
    }
    if (condition_attributes_ready) {
        (void)pthread_condattr_destroy(&condition_attributes);
    }
    if (mutex_ready) {
        (void)pthread_mutex_destroy(&queue->mutex);
    }
    free(queue->records);
    memset(queue, 0, sizeof(*queue));
    return DRAW_QUEUE_SYSTEM_ERROR;
}

int draw_owned_queue_try_push(
    DrawOwnedQueue *queue,
    DrawOwnedRecord *record)
{
    int status = DRAW_QUEUE_OK;

    if (queue == NULL || record == NULL || record->item == NULL) {
        return DRAW_QUEUE_INVALID;
    }
    if (pthread_mutex_lock(&queue->mutex) != 0) {
        return DRAW_QUEUE_SYSTEM_ERROR;
    }
    if (queue->closed) {
        status = DRAW_QUEUE_CLOSED;
    } else if (!draw_owned_queue_has_capacity_locked(queue, record)) {
        status = DRAW_QUEUE_FULL;
    } else {
        queue->records[queue->tail] = *record;
        queue->tail = (queue->tail + 1u) % queue->record_capacity;
        queue->record_count += 1u;
        queue->bytes_used += record->bytes;
        record->item = NULL;
        record->bytes = 0u;
        record->control = false;
        (void)pthread_cond_signal(&queue->not_empty);
    }
    (void)pthread_mutex_unlock(&queue->mutex);
    return status;
}

int draw_owned_queue_push_wait(
    DrawOwnedQueue *queue,
    DrawOwnedRecord *record,
    const _Atomic bool *stop_requested,
    int timeout_ms)
{
    struct timespec deadline;
    int wait_status = 0;
    int status = DRAW_QUEUE_OK;

    if (queue == NULL || record == NULL || record->item == NULL
        || timeout_ms < -1) {
        return DRAW_QUEUE_INVALID;
    }
    if (record->bytes > queue->byte_capacity
        || (!record->control
            && (queue->record_capacity == queue->reserved_control_records
                || record->bytes
                    > queue->byte_capacity - queue->reserved_control_bytes))) {
        return DRAW_QUEUE_FULL;
    }
    if (timeout_ms >= 0 && clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        return DRAW_QUEUE_SYSTEM_ERROR;
    }
    if (timeout_ms >= 0) {
        draw_owned_queue_add_timeout(&deadline, timeout_ms);
    }
    if (pthread_mutex_lock(&queue->mutex) != 0) {
        return DRAW_QUEUE_SYSTEM_ERROR;
    }

    while (!queue->closed && !draw_owned_queue_has_capacity_locked(queue, record)
           && (stop_requested == NULL
               || !atomic_load_explicit(stop_requested, memory_order_acquire))) {
        if (timeout_ms < 0) {
            wait_status = pthread_cond_wait(&queue->not_full, &queue->mutex);
        } else {
            wait_status = pthread_cond_timedwait(
                &queue->not_full,
                &queue->mutex,
                &deadline);
        }
        if (wait_status == ETIMEDOUT) {
            status = DRAW_QUEUE_TIMED_OUT;
            goto done;
        }
        if (wait_status != 0) {
            status = DRAW_QUEUE_SYSTEM_ERROR;
            goto done;
        }
    }

    if (queue->closed) {
        status = DRAW_QUEUE_CLOSED;
    } else if (stop_requested != NULL
               && atomic_load_explicit(stop_requested, memory_order_acquire)) {
        status = DRAW_QUEUE_STOPPED;
    } else {
        queue->records[queue->tail] = *record;
        queue->tail = (queue->tail + 1u) % queue->record_capacity;
        queue->record_count += 1u;
        queue->bytes_used += record->bytes;
        record->item = NULL;
        record->bytes = 0u;
        record->control = false;
        (void)pthread_cond_signal(&queue->not_empty);
    }

done:
    (void)pthread_mutex_unlock(&queue->mutex);
    return status;
}

int draw_owned_queue_try_pop(
    DrawOwnedQueue *queue,
    DrawOwnedRecord *out_record)
{
    int status = DRAW_QUEUE_OK;

    if (queue == NULL || out_record == NULL) {
        return DRAW_QUEUE_INVALID;
    }
    if (pthread_mutex_lock(&queue->mutex) != 0) {
        return DRAW_QUEUE_SYSTEM_ERROR;
    }
    if (queue->record_count == 0u) {
        status = queue->closed ? DRAW_QUEUE_CLOSED : DRAW_QUEUE_EMPTY;
    } else {
        *out_record = queue->records[queue->head];
        memset(&queue->records[queue->head], 0, sizeof(queue->records[queue->head]));
        queue->head = (queue->head + 1u) % queue->record_capacity;
        queue->record_count -= 1u;
        queue->bytes_used -= out_record->bytes;
        (void)pthread_cond_signal(&queue->not_full);
    }
    (void)pthread_mutex_unlock(&queue->mutex);
    return status;
}

int draw_owned_queue_pop_wait(
    DrawOwnedQueue *queue,
    DrawOwnedRecord *out_record,
    const _Atomic bool *stop_requested,
    int timeout_ms)
{
    struct timespec deadline;
    int wait_status = 0;
    int status = DRAW_QUEUE_OK;

    if (queue == NULL || out_record == NULL || timeout_ms < -1) {
        return DRAW_QUEUE_INVALID;
    }
    if (timeout_ms >= 0 && clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        return DRAW_QUEUE_SYSTEM_ERROR;
    }
    if (timeout_ms >= 0) {
        draw_owned_queue_add_timeout(&deadline, timeout_ms);
    }
    if (pthread_mutex_lock(&queue->mutex) != 0) {
        return DRAW_QUEUE_SYSTEM_ERROR;
    }

    while (queue->record_count == 0u && !queue->closed
           && (stop_requested == NULL
               || !atomic_load_explicit(stop_requested, memory_order_acquire))) {
        if (timeout_ms < 0) {
            wait_status = pthread_cond_wait(&queue->not_empty, &queue->mutex);
        } else {
            wait_status = pthread_cond_timedwait(
                &queue->not_empty,
                &queue->mutex,
                &deadline);
        }
        if (wait_status == ETIMEDOUT) {
            status = DRAW_QUEUE_TIMED_OUT;
            goto done;
        }
        if (wait_status != 0) {
            status = DRAW_QUEUE_SYSTEM_ERROR;
            goto done;
        }
    }

    if (queue->record_count != 0u) {
        *out_record = queue->records[queue->head];
        memset(&queue->records[queue->head], 0, sizeof(queue->records[queue->head]));
        queue->head = (queue->head + 1u) % queue->record_capacity;
        queue->record_count -= 1u;
        queue->bytes_used -= out_record->bytes;
        (void)pthread_cond_signal(&queue->not_full);
    } else if (stop_requested != NULL
               && atomic_load_explicit(stop_requested, memory_order_acquire)) {
        status = DRAW_QUEUE_STOPPED;
    } else {
        status = DRAW_QUEUE_CLOSED;
    }

done:
    (void)pthread_mutex_unlock(&queue->mutex);
    return status;
}

void draw_owned_queue_wake_all(DrawOwnedQueue *queue)
{
    if (queue == NULL) {
        return;
    }
    if (pthread_mutex_lock(&queue->mutex) == 0) {
        (void)pthread_cond_broadcast(&queue->not_empty);
        (void)pthread_cond_broadcast(&queue->not_full);
        (void)pthread_mutex_unlock(&queue->mutex);
    }
}

void draw_owned_queue_close(DrawOwnedQueue *queue)
{
    if (queue == NULL) {
        return;
    }
    if (pthread_mutex_lock(&queue->mutex) == 0) {
        queue->closed = true;
        (void)pthread_cond_broadcast(&queue->not_empty);
        (void)pthread_cond_broadcast(&queue->not_full);
        (void)pthread_mutex_unlock(&queue->mutex);
    }
}

void draw_owned_queue_destroy(
    DrawOwnedQueue *queue,
    DrawOwnedItemDestroyFn destroy_item)
{
    size_t index;

    if (queue == NULL || queue->records == NULL) {
        return;
    }
    for (index = 0u; index < queue->record_capacity; ++index) {
        if (queue->records[index].item != NULL && destroy_item != NULL) {
            destroy_item(queue->records[index].item);
        }
    }
    free(queue->records);
    queue->records = NULL;
    (void)pthread_cond_destroy(&queue->not_full);
    (void)pthread_cond_destroy(&queue->not_empty);
    (void)pthread_mutex_destroy(&queue->mutex);
    memset(queue, 0, sizeof(*queue));
}
