#include "draw_online/server.h"

#include "draw_online/owned_queue.h"
#include "draw_online/room_plugin.h"
#include "draw_online/room_request.h"
#include "draw_online/wire.h"

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DRAW_SERVER_MAX_CONNECTIONS 32u
#define DRAW_SERVER_MAX_IDENTITIES 32u
#define DRAW_SERVER_MAX_MEMBERSHIPS 64u
#define DRAW_SERVER_MAX_EPOLL_EVENTS 32
#define DRAW_SERVER_MAX_OUTBOUND_BYTES (1024u * 1024u)
#define DRAW_SERVER_DEFAULT_QUEUE_RECORDS 64u
#define DRAW_SERVER_DEFAULT_QUEUE_BYTES (256u * 1024u)
#define DRAW_SERVER_ROOM_ID 1u
#define DRAW_SERVER_LOBBY_CREDIT_LIMIT 16u
#define DRAW_SERVER_MAIN_CREDIT_LIMIT 8u
#define DRAW_SERVER_ROOM_READY_TIMEOUT_MS 3000

typedef enum DrawServerConnectionState {
    DRAW_SERVER_CONNECTION_FREE = 0,
    DRAW_SERVER_CONNECTION_PREAUTH,
    DRAW_SERVER_CONNECTION_AUTH_PENDING,
    DRAW_SERVER_CONNECTION_ACTIVE
} DrawServerConnectionState;

typedef enum DrawServerEventKind {
    DRAW_SERVER_EVENT_LISTENER = 1,
    DRAW_SERVER_EVENT_WAKEUP = 2,
    DRAW_SERVER_EVENT_CONNECTION = 3
} DrawServerEventKind;

typedef struct DrawServerEventSource {
    DrawServerEventKind kind;
    uint32_t connection_slot;
} DrawServerEventSource;

typedef struct DrawServerOutboundFrame {
    struct DrawServerOutboundFrame *next;
    size_t length;
    size_t offset;
    uint8_t bytes[];
} DrawServerOutboundFrame;

typedef struct DrawServerConnection {
    int fd;
    uint64_t generation;
    DrawServerConnectionState state;
    uint64_t bound_uid;
    int identity_index;
    uint8_t *input;
    size_t input_used;
    size_t input_capacity;
    DrawServerOutboundFrame *outbound_head;
    DrawServerOutboundFrame *outbound_tail;
    size_t outbound_bytes;
    DrawServerEventSource source;
} DrawServerConnection;

typedef struct DrawServerIdentity {
    bool used;
    uint64_t uid;
    uint8_t identity_handle[16];
    DrawConnectionRef connection;
    uint64_t expected_client_sequence;
    uint64_t next_server_sequence;
} DrawServerIdentity;

typedef struct DrawServerMembership {
    bool used;
    uint64_t uid;
    uint64_t room_id;
    uint32_t player_slot;
} DrawServerMembership;

typedef struct DrawServerPacket {
    DrawWireHeader header;
    size_t payload_length;
    uint8_t payload[];
} DrawServerPacket;

typedef struct DrawServerAuthRequest {
    uint64_t request_id;
    DrawConnectionRef connection;
} DrawServerAuthRequest;

typedef struct DrawServerAuthResult {
    uint64_t request_id;
    DrawConnectionRef connection;
    uint64_t uid;
    uint8_t identity_handle[16];
    uint32_t code;
} DrawServerAuthResult;

typedef enum DrawServerLobbyMessageKind {
    DRAW_SERVER_LOBBY_CLIENT_PACKET = 1,
    DRAW_SERVER_LOBBY_ROOM_RESULT = 2
} DrawServerLobbyMessageKind;

typedef struct DrawServerLobbyRoomResult {
    uint64_t request_id;
    DrawRoomRequestOrigin origin;
    DrawConnectionRef connection;
    uint64_t uid;
    uint64_t room_id;
    uint32_t player_slot;
    uint32_t code;
} DrawServerLobbyRoomResult;

typedef struct DrawServerLobbyMessage {
    DrawServerLobbyMessageKind kind;
    DrawConnectionRef connection;
    DrawServerPacket *packet;
    DrawServerLobbyRoomResult result;
} DrawServerLobbyMessage;

typedef struct DrawServerLobbyOutput {
    DrawConnectionRef connection;
    uint64_t uid;
    uint64_t room_id;
    uint32_t player_slot;
    uint32_t code;
} DrawServerLobbyOutput;

typedef struct DrawServerRoomInput {
    DrawRoomRecordKind kind;
    uint32_t player_slot;
    uint32_t code;
    size_t payload_length;
    uint8_t payload[];
} DrawServerRoomInput;

typedef struct DrawServerRoomOutput {
    DrawRoomRecipientKind recipients;
    uint32_t player_slot;
    size_t payload_length;
    uint8_t payload[];
} DrawServerRoomOutput;

typedef struct DrawServerRoomRuntime {
    struct DrawOnlineServer *server;
    DrawRoomContext context;
    DrawServerRoomInput *pending_input;
    DrawServerRoomInput *current_input;
} DrawServerRoomRuntime;

struct DrawOnlineServer {
    int listen_fd;
    int epoll_fd;
    int gateway_wakeup_fd;
    uint16_t bound_port;
    DrawServerEventSource listener_source;
    DrawServerEventSource wakeup_source;

    _Atomic bool stop_requested;
    _Atomic bool room_stop_requested;

    pthread_t gateway_thread;
    pthread_t auth_thread;
    pthread_t lobby_thread;
    pthread_t room_thread;
    bool gateway_thread_started;
    bool auth_thread_started;
    bool lobby_thread_started;
    bool room_thread_started;

    DrawOwnedQueue auth_requests;
    DrawOwnedQueue auth_results;
    DrawOwnedQueue lobby_inbox;
    DrawOwnedQueue lobby_outbox;
    DrawOwnedQueue room_inbox;
    DrawOwnedQueue room_outbox;
    DrawRoomRequestQueue room_requests;
    bool auth_requests_ready;
    bool auth_results_ready;
    bool lobby_inbox_ready;
    bool lobby_outbox_ready;
    bool room_inbox_ready;
    bool room_outbox_ready;
    bool room_requests_ready;

    pthread_mutex_t room_lifecycle_mutex;
    pthread_cond_t room_lifecycle_changed;
    bool room_lifecycle_ready;
    bool room_ready;
    bool room_finished;
    int room_entry_result;

    void *room_module_handle;
    DrawRoomEntryFn room_entry;
    DrawServerRoomRuntime room_runtime;

    DrawServerConnection connections[DRAW_SERVER_MAX_CONNECTIONS];
    DrawServerIdentity identities[DRAW_SERVER_MAX_IDENTITIES];
    DrawServerMembership memberships[DRAW_SERVER_MAX_MEMBERSHIPS];
    uint64_t next_auth_request_id;
};

static void draw_server_free_lobby_message(void *item)
{
    DrawServerLobbyMessage *message = item;

    if (message != NULL) {
        free(message->packet);
        free(message);
    }
}

static int draw_server_set_nonblocking_cloexec(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        return -1;
    }
    return 0;
}

static int draw_server_fill_random(void *buffer, size_t length)
{
    uint8_t *bytes = buffer;
    size_t offset = 0u;

    while (offset < length) {
        const ssize_t received = getrandom(bytes + offset, length - offset, 0);
        if (received > 0) {
            offset += (size_t)received;
            continue;
        }
        if (received < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 0;
}

static int64_t draw_server_monotonic_now_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    return (int64_t)now.tv_sec * INT64_C(1000000000) + (int64_t)now.tv_nsec;
}

static void draw_server_wake_gateway(DrawOnlineServer *server)
{
    const uint64_t one = 1u;
    ssize_t result;

    if (server->gateway_wakeup_fd < 0) {
        return;
    }
    do {
        result = write(server->gateway_wakeup_fd, &one, sizeof(one));
    } while (result < 0 && errno == EINTR);
}

static DrawConnectionRef draw_server_connection_ref(
    const DrawServerConnection *connection)
{
    DrawConnectionRef reference;

    reference.slot = connection->source.connection_slot;
    reference.generation = connection->generation;
    return reference;
}

static DrawServerConnection *draw_server_find_connection(
    DrawOnlineServer *server,
    DrawConnectionRef reference)
{
    DrawServerConnection *connection;

    if (reference.slot >= DRAW_SERVER_MAX_CONNECTIONS) {
        return NULL;
    }
    connection = &server->connections[reference.slot];
    if (connection->state == DRAW_SERVER_CONNECTION_FREE
        || connection->generation != reference.generation) {
        return NULL;
    }
    return connection;
}

static DrawServerIdentity *draw_server_identity_for_connection(
    DrawOnlineServer *server,
    DrawServerConnection *connection)
{
    DrawServerIdentity *identity;

    if (connection == NULL || connection->identity_index < 0
        || connection->identity_index >= (int)DRAW_SERVER_MAX_IDENTITIES) {
        return NULL;
    }
    identity = &server->identities[(size_t)connection->identity_index];
    if (!identity->used
        || identity->connection.slot != connection->source.connection_slot
        || identity->connection.generation != connection->generation) {
        return NULL;
    }
    return identity;
}

static DrawServerIdentity *draw_server_find_identity_by_uid(
    DrawOnlineServer *server,
    uint64_t uid)
{
    size_t index;

    for (index = 0u; index < DRAW_SERVER_MAX_IDENTITIES; ++index) {
        if (server->identities[index].used && server->identities[index].uid == uid) {
            return &server->identities[index];
        }
    }
    return NULL;
}

static DrawServerMembership *draw_server_find_membership(
    DrawOnlineServer *server,
    uint64_t uid,
    uint64_t room_id)
{
    size_t index;

    for (index = 0u; index < DRAW_SERVER_MAX_MEMBERSHIPS; ++index) {
        DrawServerMembership *membership = &server->memberships[index];
        if (membership->used && membership->uid == uid
            && membership->room_id == room_id) {
            return membership;
        }
    }
    return NULL;
}

static DrawServerMembership *draw_server_find_membership_by_slot(
    DrawOnlineServer *server,
    uint64_t room_id,
    uint32_t player_slot)
{
    size_t index;

    for (index = 0u; index < DRAW_SERVER_MAX_MEMBERSHIPS; ++index) {
        DrawServerMembership *membership = &server->memberships[index];
        if (membership->used && membership->room_id == room_id
            && membership->player_slot == player_slot) {
            return membership;
        }
    }
    return NULL;
}

static DrawServerMembership *draw_server_allocate_membership(
    DrawOnlineServer *server,
    uint64_t uid,
    uint64_t room_id)
{
    size_t index;
    uint32_t candidate_slot = 1u;

    for (;;) {
        if (draw_server_find_membership_by_slot(server, room_id, candidate_slot)
            == NULL) {
            break;
        }
        if (candidate_slot == UINT32_MAX) {
            return NULL;
        }
        candidate_slot += 1u;
    }
    for (index = 0u; index < DRAW_SERVER_MAX_MEMBERSHIPS; ++index) {
        DrawServerMembership *membership = &server->memberships[index];
        if (!membership->used) {
            membership->used = true;
            membership->uid = uid;
            membership->room_id = room_id;
            membership->player_slot = candidate_slot;
            return membership;
        }
    }
    return NULL;
}

static int draw_server_enqueue_room_input(
    DrawOnlineServer *server,
    DrawRoomRecordKind kind,
    uint32_t player_slot,
    uint32_t code,
    const void *payload,
    size_t payload_length,
    bool control)
{
    DrawServerRoomInput *input;
    DrawOwnedRecord record;
    size_t allocation_size;
    int status;

    if (payload_length > SIZE_MAX - sizeof(*input)
        || (payload_length != 0u && payload == NULL)) {
        return DRAW_QUEUE_INVALID;
    }
    allocation_size = sizeof(*input) + payload_length;
    input = malloc(allocation_size);
    if (input == NULL) {
        return DRAW_QUEUE_SYSTEM_ERROR;
    }
    input->kind = kind;
    input->player_slot = player_slot;
    input->code = code;
    input->payload_length = payload_length;
    if (payload_length != 0u) {
        memcpy(input->payload, payload, payload_length);
    }
    record.item = input;
    record.bytes = allocation_size;
    record.control = control;
    status = draw_owned_queue_try_push(&server->room_inbox, &record);
    if (status != DRAW_QUEUE_OK) {
        free(input);
    }
    return status;
}

static void draw_server_remove_memberships_for_uid(
    DrawOnlineServer *server,
    uint64_t uid,
    bool notify_room)
{
    size_t index;

    for (index = 0u; index < DRAW_SERVER_MAX_MEMBERSHIPS; ++index) {
        DrawServerMembership *membership = &server->memberships[index];
        if (!membership->used || membership->uid != uid) {
            continue;
        }
        if (notify_room) {
            (void)draw_server_enqueue_room_input(
                server,
                DRAW_ROOM_RECORD_PLAYER_REMOVED,
                membership->player_slot,
                DRAW_WIRE_CODE_OK,
                NULL,
                0u,
                true);
        }
        memset(membership, 0, sizeof(*membership));
    }
}

static void draw_server_free_outbound(DrawServerConnection *connection)
{
    DrawServerOutboundFrame *frame = connection->outbound_head;

    while (frame != NULL) {
        DrawServerOutboundFrame *next = frame->next;
        free(frame);
        frame = next;
    }
    connection->outbound_head = NULL;
    connection->outbound_tail = NULL;
    connection->outbound_bytes = 0u;
}

static void draw_server_close_connection(
    DrawOnlineServer *server,
    DrawServerConnection *connection)
{
    DrawServerIdentity *identity;

    if (connection == NULL || connection->state == DRAW_SERVER_CONNECTION_FREE) {
        return;
    }
    identity = draw_server_identity_for_connection(server, connection);
    if (identity != NULL) {
        draw_server_remove_memberships_for_uid(
            server,
            identity->uid,
            !atomic_load_explicit(&server->stop_requested, memory_order_acquire));
        memset(identity, 0, sizeof(*identity));
    }
    if (server->epoll_fd >= 0 && connection->fd >= 0) {
        (void)epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, connection->fd, NULL);
    }
    if (connection->fd >= 0) {
        (void)close(connection->fd);
    }
    free(connection->input);
    connection->input = NULL;
    connection->input_used = 0u;
    connection->input_capacity = 0u;
    draw_server_free_outbound(connection);
    connection->fd = -1;
    connection->state = DRAW_SERVER_CONNECTION_FREE;
    connection->bound_uid = 0u;
    connection->identity_index = -1;
}

static int draw_server_update_connection_events(
    DrawOnlineServer *server,
    DrawServerConnection *connection)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN | EPOLLRDHUP;
    if (connection->outbound_head != NULL) {
        event.events |= EPOLLOUT;
    }
    event.data.ptr = &connection->source;
    return epoll_ctl(server->epoll_fd, EPOLL_CTL_MOD, connection->fd, &event);
}

static int draw_server_enqueue_frame(
    DrawOnlineServer *server,
    DrawServerConnection *connection,
    const DrawWireHeader *header,
    const void *payload,
    size_t payload_length)
{
    DrawServerOutboundFrame *frame;
    uint32_t body_length;
    size_t frame_length;

    if (connection == NULL || connection->state == DRAW_SERVER_CONNECTION_FREE
        || payload_length != (size_t)header->payload_length
        || (payload_length != 0u && payload == NULL)
        || draw_wire_frame_length(header, &body_length) != DRAW_WIRE_OK) {
        return -1;
    }
    frame_length = DRAW_WIRE_LENGTH_PREFIX_SIZE + (size_t)body_length;
    if (frame_length > SIZE_MAX - sizeof(*frame)
        || frame_length > DRAW_SERVER_MAX_OUTBOUND_BYTES
        || connection->outbound_bytes
               > DRAW_SERVER_MAX_OUTBOUND_BYTES - frame_length) {
        return -1;
    }
    frame = malloc(sizeof(*frame) + frame_length);
    if (frame == NULL) {
        return -1;
    }
    frame->next = NULL;
    frame->length = frame_length;
    frame->offset = 0u;
    draw_wire_u32_encode(frame->bytes, body_length);
    if (draw_wire_header_encode(
            frame->bytes + DRAW_WIRE_LENGTH_PREFIX_SIZE,
            header)
        != DRAW_WIRE_OK) {
        free(frame);
        return -1;
    }
    if (payload_length != 0u) {
        memcpy(
            frame->bytes + DRAW_WIRE_LENGTH_PREFIX_SIZE + DRAW_WIRE_HEADER_SIZE,
            payload,
            payload_length);
    }

    if (connection->outbound_tail == NULL) {
        connection->outbound_head = frame;
    } else {
        connection->outbound_tail->next = frame;
    }
    connection->outbound_tail = frame;
    connection->outbound_bytes += frame_length;
    if (draw_server_update_connection_events(server, connection) != 0) {
        return -1;
    }
    return 0;
}

static int draw_server_send_authenticated(
    DrawOnlineServer *server,
    DrawServerConnection *connection,
    uint16_t route,
    uint16_t kind,
    uint64_t room_id,
    uint32_t player_slot,
    uint32_t code,
    const void *payload,
    size_t payload_length)
{
    DrawServerIdentity *identity;
    DrawWireHeader header;

    identity = draw_server_identity_for_connection(server, connection);
    if (identity == NULL || identity->next_server_sequence == UINT64_MAX
        || payload_length > UINT32_MAX) {
        return -1;
    }
    memset(&header, 0, sizeof(header));
    header.route = route;
    header.kind = kind;
    header.payload_length = (uint32_t)payload_length;
    header.uid = identity->uid;
    memcpy(header.identity_handle, identity->identity_handle, 16u);
    header.room_id = room_id;
    header.sequence = identity->next_server_sequence;
    header.player_slot = player_slot;
    header.code = code;
    if (draw_server_enqueue_frame(
            server,
            connection,
            &header,
            payload,
            payload_length)
        != 0) {
        return -1;
    }
    identity->next_server_sequence += 1u;
    return 0;
}

static int draw_server_validate_authenticated_packet(
    DrawOnlineServer *server,
    DrawServerConnection *connection,
    const DrawWireHeader *header)
{
    DrawServerIdentity *identity;

    if (connection->state != DRAW_SERVER_CONNECTION_ACTIVE
        || header->player_slot != 0u) {
        return -1;
    }
    identity = draw_server_identity_for_connection(server, connection);
    if (identity == NULL || connection->bound_uid != header->uid
        || identity->uid != header->uid
        || memcmp(identity->identity_handle, header->identity_handle, 16u) != 0
        || header->sequence != identity->expected_client_sequence
        || identity->expected_client_sequence == UINT64_MAX) {
        return -1;
    }
    identity->expected_client_sequence += 1u;
    return 0;
}

static DrawServerPacket *draw_server_packet_new(
    const DrawWireHeader *header,
    const uint8_t *payload)
{
    DrawServerPacket *packet;
    const size_t payload_length = (size_t)header->payload_length;

    if (payload_length > SIZE_MAX - sizeof(*packet)) {
        return NULL;
    }
    packet = malloc(sizeof(*packet) + payload_length);
    if (packet == NULL) {
        return NULL;
    }
    packet->header = *header;
    packet->payload_length = payload_length;
    if (payload_length != 0u) {
        memcpy(packet->payload, payload, payload_length);
    }
    return packet;
}

static bool draw_server_bytes_are_zero(const uint8_t *bytes, size_t length)
{
    size_t index;

    for (index = 0u; index < length; ++index) {
        if (bytes[index] != 0u) {
            return false;
        }
    }
    return true;
}

static int draw_server_route_auth_packet(
    DrawOnlineServer *server,
    DrawServerConnection *connection,
    DrawServerPacket *packet)
{
    DrawServerAuthRequest *request;
    DrawOwnedRecord record;

    if (connection->state != DRAW_SERVER_CONNECTION_PREAUTH
        || packet->header.kind != DRAW_WIRE_AUTH_LOGIN
        || packet->header.payload_length != 0u || packet->header.uid != 0u
        || packet->header.sequence != 0u || packet->header.player_slot != 0u
        || packet->header.room_id != 0u || packet->header.code != 0u
        || packet->header.flags != 0u
        || !draw_server_bytes_are_zero(
            packet->header.identity_handle,
            sizeof(packet->header.identity_handle))) {
        return -1;
    }
    request = calloc(1u, sizeof(*request));
    if (request == NULL) {
        return -1;
    }
    server->next_auth_request_id += 1u;
    if (server->next_auth_request_id == 0u) {
        server->next_auth_request_id += 1u;
    }
    request->request_id = server->next_auth_request_id;
    request->connection = draw_server_connection_ref(connection);
    record.item = request;
    record.bytes = sizeof(*request);
    record.control = false;
    if (draw_owned_queue_try_push(&server->auth_requests, &record)
        != DRAW_QUEUE_OK) {
        free(request);
        return -1;
    }
    connection->state = DRAW_SERVER_CONNECTION_AUTH_PENDING;
    return 0;
}

static int draw_server_route_lobby_packet(
    DrawOnlineServer *server,
    DrawServerConnection *connection,
    DrawServerPacket *packet)
{
    DrawServerLobbyMessage *message;
    DrawOwnedRecord record;

    if (packet->header.kind != DRAW_WIRE_LOBBY_JOIN
        || packet->header.payload_length != 0u
        || packet->header.room_id == 0u) {
        return -1;
    }
    message = calloc(1u, sizeof(*message));
    if (message == NULL) {
        return -1;
    }
    message->kind = DRAW_SERVER_LOBBY_CLIENT_PACKET;
    message->connection = draw_server_connection_ref(connection);
    message->packet = packet;
    record.item = message;
    record.bytes = sizeof(*message) + packet->payload_length;
    record.control = false;
    if (draw_owned_queue_try_push(&server->lobby_inbox, &record)
        != DRAW_QUEUE_OK) {
        message->packet = NULL;
        free(message);
        return -1;
    }
    return 1;
}

static int draw_server_route_game_packet(
    DrawOnlineServer *server,
    DrawServerConnection *connection,
    DrawServerPacket *packet)
{
    DrawServerMembership *membership;
    int status;

    if (packet->header.kind != DRAW_WIRE_GAME_DATA
        || packet->header.room_id == 0u) {
        return -1;
    }
    membership = draw_server_find_membership(
        server,
        packet->header.uid,
        packet->header.room_id);
    if (membership == NULL) {
        return -1;
    }
    status = draw_server_enqueue_room_input(
        server,
        DRAW_ROOM_RECORD_DATA,
        membership->player_slot,
        DRAW_WIRE_CODE_OK,
        packet->payload,
        packet->payload_length,
        false);
    if (status == DRAW_QUEUE_FULL) {
        return draw_server_send_authenticated(
            server,
            connection,
            DRAW_WIRE_ROUTE_GAME,
            DRAW_WIRE_GAME_RESULT,
            packet->header.room_id,
            membership->player_slot,
            DRAW_WIRE_CODE_BUSY,
            NULL,
            0u);
    }
    return status == DRAW_QUEUE_OK ? 0 : -1;
}

static int draw_server_route_packet(
    DrawOnlineServer *server,
    DrawServerConnection *connection,
    DrawServerPacket *packet)
{
    int status;

    if (packet->header.route == DRAW_WIRE_ROUTE_AUTH) {
        return draw_server_route_auth_packet(server, connection, packet);
    }
    if (draw_server_validate_authenticated_packet(
            server,
            connection,
            &packet->header)
        != 0) {
        return -1;
    }
    if (packet->header.route == DRAW_WIRE_ROUTE_LOBBY) {
        status = draw_server_route_lobby_packet(server, connection, packet);
        return status;
    }
    if (packet->header.route == DRAW_WIRE_ROUTE_GAME) {
        return draw_server_route_game_packet(server, connection, packet);
    }
    return -1;
}

static void draw_server_flush_connection(
    DrawOnlineServer *server,
    DrawServerConnection *connection)
{
    while (connection->outbound_head != NULL) {
        DrawServerOutboundFrame *frame = connection->outbound_head;
        const ssize_t written = send(
            connection->fd,
            frame->bytes + frame->offset,
            frame->length - frame->offset,
            MSG_NOSIGNAL);
        if (written > 0) {
            const size_t amount = (size_t)written;
            frame->offset += amount;
            connection->outbound_bytes -= amount;
            if (frame->offset == frame->length) {
                connection->outbound_head = frame->next;
                if (connection->outbound_head == NULL) {
                    connection->outbound_tail = NULL;
                }
                free(frame);
            }
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        draw_server_close_connection(server, connection);
        return;
    }
    if (connection->state != DRAW_SERVER_CONNECTION_FREE
        && draw_server_update_connection_events(server, connection) != 0) {
        draw_server_close_connection(server, connection);
    }
}

static void draw_server_read_connection(
    DrawOnlineServer *server,
    DrawServerConnection *connection)
{
    for (;;) {
        ssize_t received;

        if (connection->input_used == connection->input_capacity) {
            draw_server_close_connection(server, connection);
            return;
        }
        received = recv(
            connection->fd,
            connection->input + connection->input_used,
            connection->input_capacity - connection->input_used,
            0);
        if (received > 0) {
            connection->input_used += (size_t)received;
        } else if (received == 0) {
            draw_server_close_connection(server, connection);
            return;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        } else {
            draw_server_close_connection(server, connection);
            return;
        }

        while (connection->input_used >= DRAW_WIRE_LENGTH_PREFIX_SIZE) {
            const uint32_t body_length = draw_wire_u32_decode(connection->input);
            const size_t full_length = DRAW_WIRE_LENGTH_PREFIX_SIZE
                + (size_t)body_length;
            DrawWireHeader header;
            DrawServerPacket *packet;
            int route_status;

            if (body_length < DRAW_WIRE_HEADER_SIZE
                || body_length > DRAW_WIRE_HEADER_SIZE + DRAW_WIRE_MAX_PAYLOAD
                || full_length > connection->input_capacity) {
                draw_server_close_connection(server, connection);
                return;
            }
            if (connection->input_used < full_length) {
                break;
            }
            if (draw_wire_header_decode(
                    connection->input + DRAW_WIRE_LENGTH_PREFIX_SIZE,
                    &header)
                    != DRAW_WIRE_OK
                || body_length != DRAW_WIRE_HEADER_SIZE + header.payload_length) {
                draw_server_close_connection(server, connection);
                return;
            }
            packet = draw_server_packet_new(
                &header,
                connection->input + DRAW_WIRE_LENGTH_PREFIX_SIZE
                    + DRAW_WIRE_HEADER_SIZE);
            if (packet == NULL) {
                draw_server_close_connection(server, connection);
                return;
            }
            memmove(
                connection->input,
                connection->input + full_length,
                connection->input_used - full_length);
            connection->input_used -= full_length;
            route_status = draw_server_route_packet(server, connection, packet);
            if (route_status != 1) {
                free(packet);
            }
            if (route_status < 0) {
                draw_server_close_connection(server, connection);
                return;
            }
        }
    }
}

static void draw_server_accept_connections(DrawOnlineServer *server)
{
    for (;;) {
        int client_fd = accept(server->listen_fd, NULL, NULL);
        size_t slot;
        DrawServerConnection *connection = NULL;
        struct epoll_event event;

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (draw_server_set_nonblocking_cloexec(client_fd) != 0) {
            (void)close(client_fd);
            continue;
        }
        for (slot = 0u; slot < DRAW_SERVER_MAX_CONNECTIONS; ++slot) {
            if (server->connections[slot].state == DRAW_SERVER_CONNECTION_FREE) {
                connection = &server->connections[slot];
                break;
            }
        }
        if (connection == NULL) {
            (void)close(client_fd);
            continue;
        }
        connection->input_capacity = DRAW_WIRE_LENGTH_PREFIX_SIZE
            + DRAW_WIRE_HEADER_SIZE + DRAW_WIRE_MAX_PAYLOAD;
        connection->input = malloc(connection->input_capacity);
        if (connection->input == NULL) {
            connection->input_capacity = 0u;
            (void)close(client_fd);
            continue;
        }
        connection->generation += 1u;
        if (connection->generation == 0u) {
            connection->generation = 1u;
        }
        connection->fd = client_fd;
        connection->state = DRAW_SERVER_CONNECTION_PREAUTH;
        connection->bound_uid = 0u;
        connection->identity_index = -1;
        connection->input_used = 0u;

        memset(&event, 0, sizeof(event));
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.ptr = &connection->source;
        if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, client_fd, &event) != 0) {
            draw_server_close_connection(server, connection);
        }
    }
}

static void draw_server_drain_wakeup(DrawOnlineServer *server)
{
    uint64_t value;

    for (;;) {
        const ssize_t result = read(server->gateway_wakeup_fd, &value, sizeof(value));
        if (result == (ssize_t)sizeof(value)) {
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
}

static int draw_server_allocate_identity(
    DrawOnlineServer *server,
    DrawServerConnection *connection,
    const DrawServerAuthResult *result)
{
    size_t index;
    DrawServerIdentity *identity = NULL;
    DrawWireHeader header;

    if (result->uid == 0u || draw_server_find_identity_by_uid(server, result->uid)
            != NULL) {
        return -1;
    }
    for (index = 0u; index < DRAW_SERVER_MAX_IDENTITIES; ++index) {
        if (!server->identities[index].used) {
            identity = &server->identities[index];
            break;
        }
    }
    if (identity == NULL) {
        return -1;
    }
    identity->used = true;
    identity->uid = result->uid;
    memcpy(identity->identity_handle, result->identity_handle, 16u);
    identity->connection = draw_server_connection_ref(connection);
    identity->expected_client_sequence = 1u;
    identity->next_server_sequence = 2u;
    connection->identity_index = (int)index;
    connection->bound_uid = result->uid;
    connection->state = DRAW_SERVER_CONNECTION_ACTIVE;

    memset(&header, 0, sizeof(header));
    header.route = DRAW_WIRE_ROUTE_AUTH;
    header.kind = DRAW_WIRE_AUTH_RESULT;
    header.uid = result->uid;
    memcpy(header.identity_handle, result->identity_handle, 16u);
    header.sequence = 1u;
    header.code = result->code;
    if (draw_server_enqueue_frame(server, connection, &header, NULL, 0u) != 0) {
        memset(identity, 0, sizeof(*identity));
        connection->identity_index = -1;
        connection->bound_uid = 0u;
        connection->state = DRAW_SERVER_CONNECTION_PREAUTH;
        return -1;
    }
    return 0;
}

static void draw_server_drain_auth_results(DrawOnlineServer *server)
{
    for (;;) {
        DrawOwnedRecord record;
        DrawServerAuthResult *result;
        DrawServerConnection *connection;

        if (draw_owned_queue_try_pop(&server->auth_results, &record)
            != DRAW_QUEUE_OK) {
            break;
        }
        result = record.item;
        connection = draw_server_find_connection(server, result->connection);
        if (connection != NULL && result->code == DRAW_WIRE_CODE_OK
            && connection->state == DRAW_SERVER_CONNECTION_AUTH_PENDING
            && draw_server_allocate_identity(server, connection, result) != 0) {
            draw_server_close_connection(server, connection);
        } else if (connection != NULL && result->code != DRAW_WIRE_CODE_OK) {
            draw_server_close_connection(server, connection);
        }
        free(result);
    }
}

static void draw_server_drain_lobby_outputs(DrawOnlineServer *server)
{
    for (;;) {
        DrawOwnedRecord record;
        DrawServerLobbyOutput *output;
        DrawServerConnection *connection;

        if (draw_owned_queue_try_pop(&server->lobby_outbox, &record)
            != DRAW_QUEUE_OK) {
            break;
        }
        output = record.item;
        connection = draw_server_find_connection(server, output->connection);
        if (connection != NULL && connection->bound_uid == output->uid
            && draw_server_send_authenticated(
                   server,
                   connection,
                   DRAW_WIRE_ROUTE_LOBBY,
                   DRAW_WIRE_LOBBY_RESULT,
                   output->room_id,
                   output->player_slot,
                   output->code,
                   NULL,
                   0u)
                != 0) {
            draw_server_close_connection(server, connection);
        }
        free(output);
    }
}

static void draw_server_publish_room_result(
    DrawOnlineServer *server,
    const DrawRoomRequest *request,
    uint32_t player_slot,
    uint32_t code)
{
    DrawServerLobbyMessage *message;
    DrawOwnedRecord record;

    if (request->origin != DRAW_ROOM_REQUEST_ORIGIN_LOBBY) {
        (void)draw_room_request_complete_after_result_pop(
            &server->room_requests,
            request->origin,
            request->request_id);
        return;
    }
    message = calloc(1u, sizeof(*message));
    if (message == NULL) {
        (void)draw_room_request_complete_after_result_pop(
            &server->room_requests,
            request->origin,
            request->request_id);
        return;
    }
    message->kind = DRAW_SERVER_LOBBY_ROOM_RESULT;
    message->result.request_id = request->request_id;
    message->result.origin = request->origin;
    message->result.connection = request->connection;
    message->result.uid = request->uid;
    message->result.room_id = request->room_id;
    message->result.player_slot = player_slot;
    message->result.code = code;
    record.item = message;
    record.bytes = sizeof(*message);
    record.control = true;
    if (draw_owned_queue_try_push(&server->lobby_inbox, &record)
        != DRAW_QUEUE_OK) {
        free(message);
        (void)draw_room_request_complete_after_result_pop(
            &server->room_requests,
            request->origin,
            request->request_id);
    }
}

static void draw_server_apply_room_request(
    DrawOnlineServer *server,
    DrawRoomRequest *request)
{
    uint32_t code = DRAW_WIRE_CODE_INVALID;
    uint32_t player_slot = 0u;
    DrawServerConnection *connection;
    DrawServerIdentity *identity;
    DrawServerMembership *membership;

    if (request->kind != DRAW_ROOM_REQUEST_JOIN_UID
        || request->room_id != DRAW_SERVER_ROOM_ID) {
        draw_server_publish_room_result(server, request, player_slot, code);
        return;
    }
    connection = draw_server_find_connection(server, request->connection);
    identity = draw_server_identity_for_connection(server, connection);
    if (connection == NULL || identity == NULL || identity->uid != request->uid) {
        draw_server_publish_room_result(
            server,
            request,
            player_slot,
            DRAW_WIRE_CODE_UNAUTHORIZED);
        return;
    }
    membership = draw_server_find_membership(
        server,
        request->uid,
        request->room_id);
    if (membership != NULL) {
        draw_server_publish_room_result(
            server,
            request,
            membership->player_slot,
            DRAW_WIRE_CODE_OK);
        return;
    }
    membership = draw_server_allocate_membership(
        server,
        request->uid,
        request->room_id);
    if (membership == NULL) {
        draw_server_publish_room_result(
            server,
            request,
            player_slot,
            DRAW_WIRE_CODE_BUSY);
        return;
    }
    player_slot = membership->player_slot;
    if (draw_server_enqueue_room_input(
            server,
            DRAW_ROOM_RECORD_PLAYER_JOIN,
            player_slot,
            DRAW_WIRE_CODE_OK,
            NULL,
            0u,
            true)
        != DRAW_QUEUE_OK) {
        memset(membership, 0, sizeof(*membership));
        code = DRAW_WIRE_CODE_BUSY;
    } else {
        code = DRAW_WIRE_CODE_OK;
    }
    draw_server_publish_room_result(server, request, player_slot, code);
}

static void draw_server_drain_room_requests(DrawOnlineServer *server)
{
    for (;;) {
        DrawRoomRequest *request;

        if (draw_room_request_try_pop(&server->room_requests, &request)
            != DRAW_QUEUE_OK) {
            break;
        }
        draw_server_apply_room_request(server, request);
        free(request);
    }
}

static void draw_server_send_room_output_to_membership(
    DrawOnlineServer *server,
    const DrawServerRoomOutput *output,
    DrawServerMembership *membership)
{
    DrawServerIdentity *identity;
    DrawServerConnection *connection;

    identity = draw_server_find_identity_by_uid(server, membership->uid);
    if (identity == NULL) {
        return;
    }
    connection = draw_server_find_connection(server, identity->connection);
    if (connection == NULL) {
        return;
    }
    if (draw_server_send_authenticated(
            server,
            connection,
            DRAW_WIRE_ROUTE_GAME,
            DRAW_WIRE_GAME_DATA,
            membership->room_id,
            membership->player_slot,
            DRAW_WIRE_CODE_OK,
            output->payload,
            output->payload_length)
        != 0) {
        draw_server_close_connection(server, connection);
    }
}

static void draw_server_drain_room_outputs(DrawOnlineServer *server)
{
    for (;;) {
        DrawOwnedRecord record;
        DrawServerRoomOutput *output;
        size_t index;

        if (draw_owned_queue_try_pop(&server->room_outbox, &record)
            != DRAW_QUEUE_OK) {
            break;
        }
        output = record.item;
        if (output->recipients == DRAW_ROOM_RECIPIENT_ONE) {
            DrawServerMembership *membership = draw_server_find_membership_by_slot(
                server,
                DRAW_SERVER_ROOM_ID,
                output->player_slot);
            if (membership != NULL) {
                draw_server_send_room_output_to_membership(
                    server,
                    output,
                    membership);
            }
        } else if (output->recipients == DRAW_ROOM_RECIPIENT_ALL) {
            for (index = 0u; index < DRAW_SERVER_MAX_MEMBERSHIPS; ++index) {
                DrawServerMembership *membership = &server->memberships[index];
                if (membership->used
                    && membership->room_id == DRAW_SERVER_ROOM_ID) {
                    draw_server_send_room_output_to_membership(
                        server,
                        output,
                        membership);
                }
            }
        }
        free(output);
    }
}

static void draw_server_drain_gateway_queues(DrawOnlineServer *server)
{
    draw_server_drain_auth_results(server);
    draw_server_drain_lobby_outputs(server);
    draw_server_drain_room_requests(server);
    draw_server_drain_room_outputs(server);
}

static void draw_server_close_all_connections(DrawOnlineServer *server)
{
    size_t index;

    for (index = 0u; index < DRAW_SERVER_MAX_CONNECTIONS; ++index) {
        draw_server_close_connection(server, &server->connections[index]);
    }
}

static void *draw_server_gateway_thread_main(void *userdata)
{
    DrawOnlineServer *server = userdata;
    struct epoll_event events[DRAW_SERVER_MAX_EPOLL_EVENTS];

    while (!atomic_load_explicit(&server->stop_requested, memory_order_acquire)) {
        int event_count;
        int index;

        event_count = epoll_wait(
            server->epoll_fd,
            events,
            DRAW_SERVER_MAX_EPOLL_EVENTS,
            100);
        if (event_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (index = 0; index < event_count; ++index) {
            DrawServerEventSource *source = events[index].data.ptr;

            if (source == NULL) {
                continue;
            }
            if (source->kind == DRAW_SERVER_EVENT_LISTENER) {
                draw_server_accept_connections(server);
            } else if (source->kind == DRAW_SERVER_EVENT_WAKEUP) {
                draw_server_drain_wakeup(server);
            } else if (source->kind == DRAW_SERVER_EVENT_CONNECTION
                       && source->connection_slot
                           < DRAW_SERVER_MAX_CONNECTIONS) {
                DrawServerConnection *connection =
                    &server->connections[source->connection_slot];
                const uint32_t flags = events[index].events;

                if (connection->state == DRAW_SERVER_CONNECTION_FREE) {
                    continue;
                }
                if ((flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0u) {
                    draw_server_close_connection(server, connection);
                    continue;
                }
                if ((flags & EPOLLIN) != 0u) {
                    draw_server_read_connection(server, connection);
                }
                if (connection->state != DRAW_SERVER_CONNECTION_FREE
                    && (flags & EPOLLOUT) != 0u) {
                    draw_server_flush_connection(server, connection);
                }
            }
        }
        draw_server_drain_gateway_queues(server);
    }
    draw_server_drain_gateway_queues(server);
    draw_server_close_all_connections(server);
    return NULL;
}

static void *draw_server_auth_thread_main(void *userdata)
{
    DrawOnlineServer *server = userdata;

    while (!atomic_load_explicit(&server->stop_requested, memory_order_acquire)) {
        DrawOwnedRecord input_record;
        DrawServerAuthRequest *request;
        DrawServerAuthResult *result;
        DrawOwnedRecord output_record;
        int status;

        status = draw_owned_queue_pop_wait(
            &server->auth_requests,
            &input_record,
            &server->stop_requested,
            -1);
        if (status != DRAW_QUEUE_OK) {
            if (status == DRAW_QUEUE_STOPPED || status == DRAW_QUEUE_CLOSED) {
                break;
            }
            continue;
        }
        request = input_record.item;
        result = calloc(1u, sizeof(*result));
        if (result == NULL) {
            free(request);
            continue;
        }
        result->request_id = request->request_id;
        result->connection = request->connection;
        do {
            if (draw_server_fill_random(&result->uid, sizeof(result->uid)) != 0) {
                result->uid = 0u;
                break;
            }
        } while (result->uid == 0u);
        if (result->uid != 0u
            && draw_server_fill_random(
                   result->identity_handle,
                   sizeof(result->identity_handle))
                == 0) {
            result->code = DRAW_WIRE_CODE_OK;
        } else {
            result->code = DRAW_WIRE_CODE_INTERNAL;
        }
        free(request);

        output_record.item = result;
        output_record.bytes = sizeof(*result);
        output_record.control = true;
        status = draw_owned_queue_push_wait(
            &server->auth_results,
            &output_record,
            &server->stop_requested,
            -1);
        if (status != DRAW_QUEUE_OK) {
            free(result);
            break;
        }
        draw_server_wake_gateway(server);
    }
    return NULL;
}

static void draw_server_lobby_publish_output(
    DrawOnlineServer *server,
    DrawConnectionRef connection,
    uint64_t uid,
    uint64_t room_id,
    uint32_t player_slot,
    uint32_t code)
{
    DrawServerLobbyOutput *output = calloc(1u, sizeof(*output));
    DrawOwnedRecord record;

    if (output == NULL) {
        return;
    }
    output->connection = connection;
    output->uid = uid;
    output->room_id = room_id;
    output->player_slot = player_slot;
    output->code = code;
    record.item = output;
    record.bytes = sizeof(*output);
    record.control = false;
    if (draw_owned_queue_push_wait(
            &server->lobby_outbox,
            &record,
            &server->stop_requested,
            -1)
        != DRAW_QUEUE_OK) {
        free(output);
        return;
    }
    draw_server_wake_gateway(server);
}

static void *draw_server_lobby_thread_main(void *userdata)
{
    DrawOnlineServer *server = userdata;
    uint64_t next_request_id = 0u;

    while (!atomic_load_explicit(&server->stop_requested, memory_order_acquire)) {
        DrawOwnedRecord record;
        DrawServerLobbyMessage *message;
        int status = draw_owned_queue_pop_wait(
            &server->lobby_inbox,
            &record,
            &server->stop_requested,
            -1);

        if (status != DRAW_QUEUE_OK) {
            if (status == DRAW_QUEUE_STOPPED || status == DRAW_QUEUE_CLOSED) {
                break;
            }
            continue;
        }
        message = record.item;
        if (message->kind == DRAW_SERVER_LOBBY_CLIENT_PACKET
            && message->packet != NULL) {
            DrawRoomRequest *request = calloc(1u, sizeof(*request));
            if (request == NULL) {
                draw_server_lobby_publish_output(
                    server,
                    message->connection,
                    message->packet->header.uid,
                    message->packet->header.room_id,
                    0u,
                    DRAW_WIRE_CODE_INTERNAL);
            } else {
                next_request_id += 1u;
                if (next_request_id == 0u) {
                    next_request_id += 1u;
                }
                request->origin = DRAW_ROOM_REQUEST_ORIGIN_LOBBY;
                request->kind = DRAW_ROOM_REQUEST_JOIN_UID;
                request->request_id = next_request_id;
                request->uid = message->packet->header.uid;
                request->room_id = message->packet->header.room_id;
                request->connection = message->connection;
                status = draw_room_request_submit(&server->room_requests, request);
                if (status == DRAW_QUEUE_OK) {
                    draw_server_wake_gateway(server);
                } else {
                    free(request);
                    draw_server_lobby_publish_output(
                        server,
                        message->connection,
                        message->packet->header.uid,
                        message->packet->header.room_id,
                        0u,
                        status == DRAW_QUEUE_FULL ? DRAW_WIRE_CODE_BUSY
                                                  : DRAW_WIRE_CODE_INTERNAL);
                }
            }
        } else if (message->kind == DRAW_SERVER_LOBBY_ROOM_RESULT) {
            (void)draw_room_request_complete_after_result_pop(
                &server->room_requests,
                message->result.origin,
                message->result.request_id);
            draw_server_lobby_publish_output(
                server,
                message->result.connection,
                message->result.uid,
                message->result.room_id,
                message->result.player_slot,
                message->result.code);
        }
        draw_server_free_lobby_message(message);
    }
    return NULL;
}

static int draw_server_room_api_wait(void *userdata, int timeout_ms)
{
    DrawServerRoomRuntime *runtime = userdata;
    DrawOwnedRecord record;
    int status;

    if (runtime->pending_input != NULL) {
        return DRAW_ROOM_OK;
    }
    status = draw_owned_queue_pop_wait(
        &runtime->server->room_inbox,
        &record,
        &runtime->server->room_stop_requested,
        timeout_ms);
    if (status == DRAW_QUEUE_OK) {
        runtime->pending_input = record.item;
        return DRAW_ROOM_OK;
    }
    if (status == DRAW_QUEUE_TIMED_OUT) {
        return DRAW_ROOM_EMPTY;
    }
    if (status == DRAW_QUEUE_STOPPED || status == DRAW_QUEUE_CLOSED) {
        return DRAW_ROOM_STOPPED;
    }
    return DRAW_ROOM_ERROR;
}

static int draw_server_room_api_read(void *userdata, DrawRoomInput *out_input)
{
    DrawServerRoomRuntime *runtime = userdata;
    DrawOwnedRecord record;
    DrawServerRoomInput *input;
    int status;

    if (out_input == NULL) {
        return DRAW_ROOM_INVALID;
    }
    free(runtime->current_input);
    runtime->current_input = NULL;
    if (runtime->pending_input != NULL) {
        input = runtime->pending_input;
        runtime->pending_input = NULL;
    } else {
        status = draw_owned_queue_try_pop(&runtime->server->room_inbox, &record);
        if (status != DRAW_QUEUE_OK) {
            return status == DRAW_QUEUE_EMPTY ? DRAW_ROOM_EMPTY : DRAW_ROOM_STOPPED;
        }
        input = record.item;
    }
    runtime->current_input = input;
    out_input->kind = input->kind;
    out_input->player_slot = input->player_slot;
    out_input->code = input->code;
    out_input->payload = input->payload;
    out_input->payload_length = input->payload_length;
    return DRAW_ROOM_OK;
}

static int draw_server_room_api_write(
    void *userdata,
    const DrawRoomOutput *output)
{
    DrawServerRoomRuntime *runtime = userdata;
    DrawServerRoomOutput *owned;
    DrawOwnedRecord record;
    size_t allocation_size;
    int status;

    if (output == NULL || output->payload_length > SIZE_MAX - sizeof(*owned)
        || (output->payload_length != 0u && output->payload == NULL)
        || (output->recipients != DRAW_ROOM_RECIPIENT_ONE
            && output->recipients != DRAW_ROOM_RECIPIENT_ALL)) {
        return DRAW_ROOM_INVALID;
    }
    allocation_size = sizeof(*owned) + output->payload_length;
    owned = malloc(allocation_size);
    if (owned == NULL) {
        return DRAW_ROOM_ERROR;
    }
    owned->recipients = output->recipients;
    owned->player_slot = output->player_slot;
    owned->payload_length = output->payload_length;
    if (output->payload_length != 0u) {
        memcpy(owned->payload, output->payload, output->payload_length);
    }
    record.item = owned;
    record.bytes = allocation_size;
    record.control = false;
    status = draw_owned_queue_try_push(&runtime->server->room_outbox, &record);
    if (status != DRAW_QUEUE_OK) {
        free(owned);
        if (status == DRAW_QUEUE_FULL) {
            return DRAW_ROOM_FULL;
        }
        return status == DRAW_QUEUE_CLOSED ? DRAW_ROOM_STOPPED : DRAW_ROOM_ERROR;
    }
    draw_server_wake_gateway(runtime->server);
    return DRAW_ROOM_OK;
}

static bool draw_server_room_api_stop_requested(void *userdata)
{
    DrawServerRoomRuntime *runtime = userdata;

    return atomic_load_explicit(
        &runtime->server->room_stop_requested,
        memory_order_acquire);
}

static int draw_server_room_api_mark_ready(void *userdata)
{
    DrawServerRoomRuntime *runtime = userdata;
    DrawOnlineServer *server = runtime->server;
    int result = DRAW_ROOM_OK;

    if (pthread_mutex_lock(&server->room_lifecycle_mutex) != 0) {
        return DRAW_ROOM_ERROR;
    }
    if (server->room_ready || server->room_finished) {
        result = DRAW_ROOM_INVALID;
    } else {
        server->room_ready = true;
        (void)pthread_cond_broadcast(&server->room_lifecycle_changed);
    }
    (void)pthread_mutex_unlock(&server->room_lifecycle_mutex);
    return result;
}

static int64_t draw_server_room_api_monotonic_now_ns(void *userdata)
{
    (void)userdata;
    return draw_server_monotonic_now_ns();
}

static void *draw_server_room_thread_main(void *userdata)
{
    DrawServerRoomRuntime *runtime = userdata;
    DrawOnlineServer *server = runtime->server;
    const int result = server->room_entry(&runtime->context);

    free(runtime->pending_input);
    runtime->pending_input = NULL;
    free(runtime->current_input);
    runtime->current_input = NULL;
    if (pthread_mutex_lock(&server->room_lifecycle_mutex) == 0) {
        server->room_finished = true;
        server->room_entry_result = result;
        (void)pthread_cond_broadcast(&server->room_lifecycle_changed);
        (void)pthread_mutex_unlock(&server->room_lifecycle_mutex);
    }
    return NULL;
}

static int draw_server_init_queue_set(
    DrawOnlineServer *server,
    size_t records,
    size_t bytes)
{
    const size_t lobby_reserve = DRAW_SERVER_LOBBY_CREDIT_LIMIT;
    const size_t lifecycle_reserve = records >= 8u ? 8u : 1u;
    const size_t lifecycle_bytes = bytes >= 4096u ? 4096u : bytes / 4u;

    if (records < 8u || bytes < 4096u || lobby_reserve >= records) {
        return -1;
    }
    if (draw_owned_queue_init(&server->auth_requests, records, bytes, 0u, 0u)
        != DRAW_QUEUE_OK) {
        return -1;
    }
    server->auth_requests_ready = true;
    if (draw_owned_queue_init(
            &server->auth_results,
            records,
            bytes,
            records,
            bytes)
        != DRAW_QUEUE_OK) {
        return -1;
    }
    server->auth_results_ready = true;
    if (draw_owned_queue_init(
            &server->lobby_inbox,
            records,
            bytes,
            lobby_reserve,
            lobby_reserve * sizeof(DrawServerLobbyMessage))
        != DRAW_QUEUE_OK) {
        return -1;
    }
    server->lobby_inbox_ready = true;
    if (draw_owned_queue_init(&server->lobby_outbox, records, bytes, 0u, 0u)
        != DRAW_QUEUE_OK) {
        return -1;
    }
    server->lobby_outbox_ready = true;
    if (draw_owned_queue_init(
            &server->room_inbox,
            records,
            bytes,
            lifecycle_reserve,
            lifecycle_bytes)
        != DRAW_QUEUE_OK) {
        return -1;
    }
    server->room_inbox_ready = true;
    if (draw_owned_queue_init(&server->room_outbox, records, bytes, 0u, 0u)
        != DRAW_QUEUE_OK) {
        return -1;
    }
    server->room_outbox_ready = true;
    if (draw_room_request_queue_init(
            &server->room_requests,
            records,
            DRAW_SERVER_LOBBY_CREDIT_LIMIT,
            DRAW_SERVER_MAIN_CREDIT_LIMIT)
        != DRAW_QUEUE_OK) {
        return -1;
    }
    server->room_requests_ready = true;
    return 0;
}

static int draw_server_init_listener(
    DrawOnlineServer *server,
    const char *bind_host,
    uint16_t port)
{
    struct sockaddr_in address;
    socklen_t address_length = (socklen_t)sizeof(address);
    int reuse = 1;
    struct epoll_event event;

    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        fprintf(stderr, "draw online listener: socket: %s\n", strerror(errno));
        return -1;
    }
    if (draw_server_set_nonblocking_cloexec(server->listen_fd) != 0) {
        fprintf(stderr, "draw online listener: fcntl: %s\n", strerror(errno));
        return -1;
    }
    (void)setsockopt(
        server->listen_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse,
        (socklen_t)sizeof(reuse));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_host, &address.sin_addr) != 1) {
        fprintf(stderr, "draw online listener: invalid bind address\n");
        return -1;
    }
    if (bind(
            server->listen_fd,
            (const struct sockaddr *)&address,
            (socklen_t)sizeof(address))
        != 0) {
        fprintf(stderr, "draw online listener: bind: %s\n", strerror(errno));
        return -1;
    }
    if (listen(server->listen_fd, 32) != 0) {
        fprintf(stderr, "draw online listener: listen: %s\n", strerror(errno));
        return -1;
    }
    if (getsockname(
            server->listen_fd,
            (struct sockaddr *)&address,
            &address_length)
        != 0) {
        fprintf(stderr, "draw online listener: getsockname: %s\n", strerror(errno));
        return -1;
    }
    server->bound_port = ntohs(address.sin_port);

    server->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    server->gateway_wakeup_fd = eventfd(0u, EFD_NONBLOCK | EFD_CLOEXEC);
    if (server->epoll_fd < 0) {
        fprintf(stderr, "draw online listener: epoll_create1: %s\n", strerror(errno));
        return -1;
    }
    if (server->gateway_wakeup_fd < 0) {
        fprintf(stderr, "draw online listener: eventfd: %s\n", strerror(errno));
        return -1;
    }

    server->listener_source.kind = DRAW_SERVER_EVENT_LISTENER;
    server->wakeup_source.kind = DRAW_SERVER_EVENT_WAKEUP;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.ptr = &server->listener_source;
    if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->listen_fd, &event)
        != 0) {
        fprintf(stderr, "draw online listener: epoll add listener: %s\n",
            strerror(errno));
        return -1;
    }
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.ptr = &server->wakeup_source;
    if (epoll_ctl(
            server->epoll_fd,
            EPOLL_CTL_ADD,
            server->gateway_wakeup_fd,
            &event)
        != 0) {
        fprintf(stderr, "draw online listener: epoll add wakeup: %s\n",
            strerror(errno));
        return -1;
    }
    return 0;
}

static int draw_server_load_room(
    DrawOnlineServer *server,
    const char *room_module_path)
{
    void *symbol;

    server->room_module_handle = dlopen(room_module_path, RTLD_NOW | RTLD_LOCAL);
    if (server->room_module_handle == NULL) {
        return -1;
    }
    (void)dlerror();
    symbol = dlsym(server->room_module_handle, DRAW_ROOM_ENTRY_SYMBOL);
    if (symbol == NULL || dlerror() != NULL) {
        return -1;
    }
    memcpy(&server->room_entry, &symbol, sizeof(server->room_entry));
    return server->room_entry == NULL ? -1 : 0;
}

static int draw_server_init_room_lifecycle(DrawOnlineServer *server)
{
    pthread_condattr_t attributes;

    if (pthread_mutex_init(&server->room_lifecycle_mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_condattr_init(&attributes) != 0) {
        (void)pthread_mutex_destroy(&server->room_lifecycle_mutex);
        return -1;
    }
    if (pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC) != 0
        || pthread_cond_init(&server->room_lifecycle_changed, &attributes) != 0) {
        (void)pthread_condattr_destroy(&attributes);
        (void)pthread_mutex_destroy(&server->room_lifecycle_mutex);
        return -1;
    }
    (void)pthread_condattr_destroy(&attributes);
    server->room_lifecycle_ready = true;
    return 0;
}

static int draw_server_wait_room_ready(DrawOnlineServer *server)
{
    struct timespec deadline;
    int wait_status = 0;
    bool ready;

    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        return -1;
    }
    deadline.tv_sec += DRAW_SERVER_ROOM_READY_TIMEOUT_MS / 1000;
    deadline.tv_nsec +=
        (long)(DRAW_SERVER_ROOM_READY_TIMEOUT_MS % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    if (pthread_mutex_lock(&server->room_lifecycle_mutex) != 0) {
        return -1;
    }
    while (!server->room_ready && !server->room_finished && wait_status == 0) {
        wait_status = pthread_cond_timedwait(
            &server->room_lifecycle_changed,
            &server->room_lifecycle_mutex,
            &deadline);
    }
    ready = server->room_ready && !server->room_finished;
    (void)pthread_mutex_unlock(&server->room_lifecycle_mutex);
    return ready ? 0 : -1;
}

int draw_online_server_start(
    DrawOnlineServer **out_server,
    const DrawOnlineServerOptions *options)
{
    DrawOnlineServer *server;
    const char *bind_host;
    size_t records;
    size_t bytes;
    size_t index;

    if (out_server == NULL || options == NULL
        || options->room_module_path == NULL) {
        return -1;
    }
    *out_server = NULL;
    bind_host = options->bind_host != NULL ? options->bind_host : "127.0.0.1";
    records = options->queue_records != 0u ? options->queue_records
                                          : DRAW_SERVER_DEFAULT_QUEUE_RECORDS;
    bytes = options->queue_bytes != 0u ? options->queue_bytes
                                      : DRAW_SERVER_DEFAULT_QUEUE_BYTES;

    server = calloc(1u, sizeof(*server));
    if (server == NULL) {
        return -1;
    }
    server->listen_fd = -1;
    server->epoll_fd = -1;
    server->gateway_wakeup_fd = -1;
    for (index = 0u; index < DRAW_SERVER_MAX_CONNECTIONS; ++index) {
        server->connections[index].fd = -1;
        server->connections[index].identity_index = -1;
        server->connections[index].source.kind = DRAW_SERVER_EVENT_CONNECTION;
        server->connections[index].source.connection_slot = (uint32_t)index;
    }
    atomic_init(&server->stop_requested, false);
    atomic_init(&server->room_stop_requested, false);

    if (draw_server_init_queue_set(server, records, bytes) != 0) {
        fprintf(stderr, "draw_online_server_start: queue initialization failed\n");
        draw_online_server_destroy(server);
        return -1;
    }
    if (draw_server_init_listener(server, bind_host, options->port) != 0) {
        fprintf(stderr, "draw_online_server_start: listener initialization failed\n");
        draw_online_server_destroy(server);
        return -1;
    }
    if (draw_server_init_room_lifecycle(server) != 0) {
        fprintf(stderr, "draw_online_server_start: lifecycle initialization failed\n");
        draw_online_server_destroy(server);
        return -1;
    }
    if (draw_server_load_room(server, options->room_module_path) != 0) {
        const char *loader_error = dlerror();
        fprintf(stderr, "draw_online_server_start: room module load failed: %s\n",
            loader_error != NULL ? loader_error : "unknown error");
        draw_online_server_destroy(server);
        return -1;
    }

    server->room_runtime.server = server;
    server->room_runtime.context.abi_version = DRAW_ROOM_ABI_VERSION;
    server->room_runtime.context.room_id = DRAW_SERVER_ROOM_ID;
    server->room_runtime.context.userdata = &server->room_runtime;
    server->room_runtime.context.api.wait = draw_server_room_api_wait;
    server->room_runtime.context.api.read = draw_server_room_api_read;
    server->room_runtime.context.api.write = draw_server_room_api_write;
    server->room_runtime.context.api.stop_requested =
        draw_server_room_api_stop_requested;
    server->room_runtime.context.api.mark_ready = draw_server_room_api_mark_ready;
    server->room_runtime.context.api.monotonic_now_ns =
        draw_server_room_api_monotonic_now_ns;

    if (pthread_create(
            &server->room_thread,
            NULL,
            draw_server_room_thread_main,
            &server->room_runtime)
        != 0) {
        draw_online_server_destroy(server);
        return -1;
    }
    server->room_thread_started = true;
    if (draw_server_wait_room_ready(server) != 0
        || pthread_create(
               &server->auth_thread,
               NULL,
               draw_server_auth_thread_main,
               server)
            != 0) {
        draw_online_server_destroy(server);
        return -1;
    }
    server->auth_thread_started = true;
    if (pthread_create(
            &server->lobby_thread,
            NULL,
            draw_server_lobby_thread_main,
            server)
        != 0) {
        draw_online_server_destroy(server);
        return -1;
    }
    server->lobby_thread_started = true;
    if (pthread_create(
            &server->gateway_thread,
            NULL,
            draw_server_gateway_thread_main,
            server)
        != 0) {
        draw_online_server_destroy(server);
        return -1;
    }
    server->gateway_thread_started = true;
    *out_server = server;
    return 0;
}

uint16_t draw_online_server_port(const DrawOnlineServer *server)
{
    return server != NULL ? server->bound_port : 0u;
}

void draw_online_server_stop(DrawOnlineServer *server)
{
    bool already_stopped;

    if (server == NULL) {
        return;
    }
    already_stopped = atomic_exchange_explicit(
        &server->stop_requested,
        true,
        memory_order_acq_rel);
    atomic_store_explicit(
        &server->room_stop_requested,
        true,
        memory_order_release);
    if (!already_stopped) {
        if (server->auth_requests_ready) {
            draw_owned_queue_close(&server->auth_requests);
        }
        if (server->auth_results_ready) {
            draw_owned_queue_close(&server->auth_results);
        }
        if (server->lobby_inbox_ready) {
            draw_owned_queue_close(&server->lobby_inbox);
        }
        if (server->lobby_outbox_ready) {
            draw_owned_queue_close(&server->lobby_outbox);
        }
        if (server->room_inbox_ready) {
            draw_owned_queue_close(&server->room_inbox);
        }
        if (server->room_outbox_ready) {
            draw_owned_queue_close(&server->room_outbox);
        }
        if (server->room_requests_ready) {
            draw_room_request_queue_close(&server->room_requests);
        }
        draw_server_wake_gateway(server);
    }
    if (server->gateway_thread_started) {
        (void)pthread_join(server->gateway_thread, NULL);
        server->gateway_thread_started = false;
    }
    if (server->auth_thread_started) {
        (void)pthread_join(server->auth_thread, NULL);
        server->auth_thread_started = false;
    }
    if (server->lobby_thread_started) {
        (void)pthread_join(server->lobby_thread, NULL);
        server->lobby_thread_started = false;
    }
    if (server->room_thread_started) {
        (void)pthread_join(server->room_thread, NULL);
        server->room_thread_started = false;
    }
}

void draw_online_server_destroy(DrawOnlineServer *server)
{
    if (server == NULL) {
        return;
    }
    draw_online_server_stop(server);
    draw_server_close_all_connections(server);

    if (server->auth_requests_ready) {
        draw_owned_queue_destroy(&server->auth_requests, free);
    }
    if (server->auth_results_ready) {
        draw_owned_queue_destroy(&server->auth_results, free);
    }
    if (server->lobby_inbox_ready) {
        draw_owned_queue_destroy(
            &server->lobby_inbox,
            draw_server_free_lobby_message);
    }
    if (server->lobby_outbox_ready) {
        draw_owned_queue_destroy(&server->lobby_outbox, free);
    }
    if (server->room_inbox_ready) {
        draw_owned_queue_destroy(&server->room_inbox, free);
    }
    if (server->room_outbox_ready) {
        draw_owned_queue_destroy(&server->room_outbox, free);
    }
    if (server->room_requests_ready) {
        draw_room_request_queue_destroy(&server->room_requests);
    }
    if (server->room_lifecycle_ready) {
        (void)pthread_cond_destroy(&server->room_lifecycle_changed);
        (void)pthread_mutex_destroy(&server->room_lifecycle_mutex);
    }
    if (server->room_module_handle != NULL) {
        (void)dlclose(server->room_module_handle);
    }
    if (server->gateway_wakeup_fd >= 0) {
        (void)close(server->gateway_wakeup_fd);
    }
    if (server->epoll_fd >= 0) {
        (void)close(server->epoll_fd);
    }
    if (server->listen_fd >= 0) {
        (void)close(server->listen_fd);
    }
    free(server);
}
