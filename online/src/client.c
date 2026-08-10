#include "draw_online/client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct DrawOnlineClient {
    int fd;
    bool active;
    uint64_t uid;
    uint8_t identity_handle[16];
    uint64_t next_client_sequence;
    uint64_t expected_server_sequence;
};

static int draw_client_set_nonblocking_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        return -1;
    }
    return 0;
}

static int draw_client_wait_fd(int fd, short events, int timeout_ms)
{
    struct pollfd descriptor;
    int result;

    descriptor.fd = fd;
    descriptor.events = events;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1u, timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result <= 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return -1;
    }
    return (descriptor.revents & events) != 0 ? 0 : -1;
}

static int draw_client_send_all(
    DrawOnlineClient *client,
    const uint8_t *bytes,
    size_t length,
    int timeout_ms)
{
    size_t offset = 0u;

    while (offset < length) {
        const ssize_t written = send(
            client->fd,
            bytes + offset,
            length - offset,
            MSG_NOSIGNAL);
        if (written > 0) {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)
            && draw_client_wait_fd(client->fd, POLLOUT, timeout_ms) == 0) {
            continue;
        }
        return -1;
    }
    return 0;
}

static int draw_client_receive_all(
    DrawOnlineClient *client,
    uint8_t *bytes,
    size_t length,
    int timeout_ms)
{
    size_t offset = 0u;

    while (offset < length) {
        const ssize_t received = recv(client->fd, bytes + offset, length - offset, 0);
        if (received > 0) {
            offset += (size_t)received;
            continue;
        }
        if (received == 0) {
            return -1;
        }
        if (errno == EINTR) {
            continue;
        }
        if ((errno == EAGAIN || errno == EWOULDBLOCK)
            && draw_client_wait_fd(client->fd, POLLIN, timeout_ms) == 0) {
            continue;
        }
        return -1;
    }
    return 0;
}

static int draw_client_send_frame(
    DrawOnlineClient *client,
    const DrawWireHeader *header,
    const void *payload,
    size_t payload_length,
    int timeout_ms)
{
    uint32_t body_length;
    size_t frame_length;
    uint8_t *frame;
    int result;

    if (payload_length != (size_t)header->payload_length
        || (payload_length != 0u && payload == NULL)
        || draw_wire_frame_length(header, &body_length) != DRAW_WIRE_OK) {
        return -1;
    }
    frame_length = DRAW_WIRE_LENGTH_PREFIX_SIZE + (size_t)body_length;
    frame = malloc(frame_length);
    if (frame == NULL) {
        return -1;
    }
    draw_wire_u32_encode(frame, body_length);
    if (draw_wire_header_encode(
            frame + DRAW_WIRE_LENGTH_PREFIX_SIZE,
            header)
        != DRAW_WIRE_OK) {
        free(frame);
        return -1;
    }
    if (payload_length != 0u) {
        memcpy(
            frame + DRAW_WIRE_LENGTH_PREFIX_SIZE + DRAW_WIRE_HEADER_SIZE,
            payload,
            payload_length);
    }
    result = draw_client_send_all(client, frame, frame_length, timeout_ms);
    free(frame);
    return result;
}

static int draw_client_receive_frame(
    DrawOnlineClient *client,
    DrawOnlineClientEvent *out_event,
    int timeout_ms)
{
    uint8_t prefix[DRAW_WIRE_LENGTH_PREFIX_SIZE];
    uint8_t header_wire[DRAW_WIRE_HEADER_SIZE];
    uint32_t body_length;
    DrawOnlineClientEvent event;

    memset(&event, 0, sizeof(event));
    if (draw_client_receive_all(client, prefix, sizeof(prefix), timeout_ms) != 0) {
        return -1;
    }
    body_length = draw_wire_u32_decode(prefix);
    if (body_length < DRAW_WIRE_HEADER_SIZE
        || body_length > DRAW_WIRE_HEADER_SIZE + DRAW_WIRE_MAX_PAYLOAD
        || draw_client_receive_all(
               client,
               header_wire,
               sizeof(header_wire),
               timeout_ms)
            != 0
        || draw_wire_header_decode(header_wire, &event.header) != DRAW_WIRE_OK
        || body_length != DRAW_WIRE_HEADER_SIZE + event.header.payload_length) {
        return -1;
    }
    event.payload_length = (size_t)event.header.payload_length;
    if (event.payload_length != 0u) {
        event.payload = malloc(event.payload_length);
        if (event.payload == NULL
            || draw_client_receive_all(
                   client,
                   event.payload,
                   event.payload_length,
                   timeout_ms)
                != 0) {
            free(event.payload);
            return -1;
        }
    }
    *out_event = event;
    return 0;
}

static int draw_client_send_authenticated(
    DrawOnlineClient *client,
    uint16_t route,
    uint16_t kind,
    uint64_t room_id,
    const void *payload,
    size_t payload_length,
    int timeout_ms)
{
    DrawWireHeader header;

    if (!client->active || client->next_client_sequence == UINT64_MAX
        || payload_length > UINT32_MAX) {
        return -1;
    }
    memset(&header, 0, sizeof(header));
    header.route = route;
    header.kind = kind;
    header.payload_length = (uint32_t)payload_length;
    header.uid = client->uid;
    memcpy(header.identity_handle, client->identity_handle, 16u);
    header.room_id = room_id;
    header.sequence = client->next_client_sequence;
    if (draw_client_send_frame(
            client,
            &header,
            payload,
            payload_length,
            timeout_ms)
        != 0) {
        return -1;
    }
    client->next_client_sequence += 1u;
    return 0;
}

int draw_online_client_connect(
    DrawOnlineClient **out_client,
    const char *host,
    uint16_t port,
    int timeout_ms)
{
    DrawOnlineClient *client;
    struct sockaddr_in address;
    int socket_error = 0;
    socklen_t socket_error_length = (socklen_t)sizeof(socket_error);
    int result;

    if (out_client == NULL || host == NULL || port == 0u || timeout_ms < 0) {
        return -1;
    }
    *out_client = NULL;
    client = calloc(1u, sizeof(*client));
    if (client == NULL) {
        return -1;
    }
    client->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client->fd < 0 || draw_client_set_nonblocking_cloexec(client->fd) != 0) {
        draw_online_client_close(client);
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1) {
        draw_online_client_close(client);
        return -1;
    }
    result = connect(
        client->fd,
        (const struct sockaddr *)&address,
        (socklen_t)sizeof(address));
    if (result != 0) {
        if (errno != EINPROGRESS
            || draw_client_wait_fd(client->fd, POLLOUT, timeout_ms) != 0
            || getsockopt(
                   client->fd,
                   SOL_SOCKET,
                   SO_ERROR,
                   &socket_error,
                   &socket_error_length)
                != 0
            || socket_error != 0) {
            draw_online_client_close(client);
            return -1;
        }
    }
    *out_client = client;
    return 0;
}

int draw_online_client_login(DrawOnlineClient *client, int timeout_ms)
{
    DrawWireHeader header;
    DrawOnlineClientEvent event;

    if (client == NULL || client->active) {
        return -1;
    }
    memset(&header, 0, sizeof(header));
    header.route = DRAW_WIRE_ROUTE_AUTH;
    header.kind = DRAW_WIRE_AUTH_LOGIN;
    if (draw_client_send_frame(client, &header, NULL, 0u, timeout_ms) != 0
        || draw_client_receive_frame(client, &event, timeout_ms) != 0) {
        return -1;
    }
    if (event.header.route != DRAW_WIRE_ROUTE_AUTH
        || event.header.kind != DRAW_WIRE_AUTH_RESULT
        || event.header.code != DRAW_WIRE_CODE_OK || event.header.uid == 0u
        || event.header.sequence != 1u || event.payload_length != 0u) {
        draw_online_client_event_release(&event);
        return -1;
    }
    client->uid = event.header.uid;
    memcpy(client->identity_handle, event.header.identity_handle, 16u);
    client->next_client_sequence = 1u;
    client->expected_server_sequence = 2u;
    client->active = true;
    draw_online_client_event_release(&event);
    return 0;
}

int draw_online_client_join(
    DrawOnlineClient *client,
    uint64_t room_id,
    uint32_t *out_player_slot,
    int timeout_ms)
{
    DrawOnlineClientEvent event;

    if (client == NULL || out_player_slot == NULL || room_id == 0u
        || draw_client_send_authenticated(
               client,
               DRAW_WIRE_ROUTE_LOBBY,
               DRAW_WIRE_LOBBY_JOIN,
               room_id,
               NULL,
               0u,
               timeout_ms)
            != 0
        || draw_online_client_receive(client, &event, timeout_ms) != 0) {
        return -1;
    }
    if (event.header.route != DRAW_WIRE_ROUTE_LOBBY
        || event.header.kind != DRAW_WIRE_LOBBY_RESULT
        || event.header.room_id != room_id
        || event.header.code != DRAW_WIRE_CODE_OK
        || event.header.player_slot == 0u || event.payload_length != 0u) {
        draw_online_client_event_release(&event);
        return -1;
    }
    *out_player_slot = event.header.player_slot;
    draw_online_client_event_release(&event);
    return 0;
}

int draw_online_client_send_game(
    DrawOnlineClient *client,
    uint64_t room_id,
    const void *payload,
    size_t payload_length,
    int timeout_ms)
{
    if (client == NULL || room_id == 0u || payload_length > DRAW_WIRE_MAX_PAYLOAD) {
        return -1;
    }
    return draw_client_send_authenticated(
        client,
        DRAW_WIRE_ROUTE_GAME,
        DRAW_WIRE_GAME_DATA,
        room_id,
        payload,
        payload_length,
        timeout_ms);
}

int draw_online_client_receive(
    DrawOnlineClient *client,
    DrawOnlineClientEvent *out_event,
    int timeout_ms)
{
    DrawOnlineClientEvent event;

    if (client == NULL || out_event == NULL || !client->active
        || draw_client_receive_frame(client, &event, timeout_ms) != 0) {
        return -1;
    }
    if (event.header.uid != client->uid
        || memcmp(event.header.identity_handle, client->identity_handle, 16u) != 0
        || event.header.sequence != client->expected_server_sequence
        || client->expected_server_sequence == UINT64_MAX) {
        draw_online_client_event_release(&event);
        return -1;
    }
    client->expected_server_sequence += 1u;
    *out_event = event;
    return 0;
}

void draw_online_client_event_release(DrawOnlineClientEvent *event)
{
    if (event != NULL) {
        free(event->payload);
        memset(event, 0, sizeof(*event));
    }
}

uint64_t draw_online_client_uid(const DrawOnlineClient *client)
{
    return client != NULL ? client->uid : 0u;
}

void draw_online_client_close(DrawOnlineClient *client)
{
    if (client == NULL) {
        return;
    }
    if (client->fd >= 0) {
        (void)close(client->fd);
    }
    memset(client->identity_handle, 0, sizeof(client->identity_handle));
    free(client);
}
