/* ============================================================
 * Non-blocking TCP socket layer: ring buffers, poll loop,
 * send/recv, protocol integration.
 *
 * @deps-implements: net/socket.h
 * @deps-requires: net/protocol.h (NetMsg, NET_FRAME_HEADER_SIZE, NET_MAX_MSG_SIZE, net_frame_write, net_frame_read, net_msg_write_framed, net_msg_deserialize), sys/socket.h, netinet/in.h, unistd.h, fcntl.h, poll.h, errno.h, string.h, stdio.h, arpa/inet.h, stdlib.h
 * @deps-last-changed: 2026-03-22 — Initial creation
 * ============================================================ */

#include "net/socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ================================================================
 * Ring Buffer (static internal)
 * ================================================================ */

static void ringbuf_init(NetRingBuf *rb)
{
    rb->head = 0;
    rb->tail = 0;
}

static size_t ringbuf_read_avail(const NetRingBuf *rb)
{
    return rb->tail - rb->head;
}

static size_t ringbuf_write_space(const NetRingBuf *rb)
{
    return NET_RINGBUF_CAPACITY - (rb->tail - rb->head);
}

static size_t ringbuf_write(NetRingBuf *rb, const uint8_t *data, size_t len)
{
    size_t space = ringbuf_write_space(rb);
    if (len > space)
        len = space;
    if (len == 0)
        return 0;

    size_t tail_off = rb->tail & NET_RINGBUF_MASK;
    size_t first = NET_RINGBUF_CAPACITY - tail_off; /* bytes to end */
    if (first > len)
        first = len;
    memcpy(rb->data + tail_off, data, first);
    if (len > first)
        memcpy(rb->data, data + first, len - first);
    rb->tail += len;
    return len;
}

__attribute__((unused))
static size_t ringbuf_read(NetRingBuf *rb, uint8_t *out, size_t len)
{
    size_t avail = ringbuf_read_avail(rb);
    if (len > avail)
        len = avail;
    if (len == 0)
        return 0;

    size_t head_off = rb->head & NET_RINGBUF_MASK;
    size_t first = NET_RINGBUF_CAPACITY - head_off;
    if (first > len)
        first = len;
    memcpy(out, rb->data + head_off, first);
    if (len > first)
        memcpy(out + first, rb->data, len - first);
    rb->head += len;
    return len;
}

static size_t ringbuf_peek(const NetRingBuf *rb, uint8_t *out, size_t len)
{
    size_t avail = ringbuf_read_avail(rb);
    if (len > avail)
        len = avail;
    if (len == 0)
        return 0;

    size_t head_off = rb->head & NET_RINGBUF_MASK;
    size_t first = NET_RINGBUF_CAPACITY - head_off;
    if (first > len)
        first = len;
    memcpy(out, rb->data + head_off, first);
    if (len > first)
        memcpy(out + first, rb->data, len - first);
    return len;
}

static void ringbuf_consume(NetRingBuf *rb, size_t len)
{
    size_t avail = ringbuf_read_avail(rb);
    if (len > avail)
        len = avail;
    rb->head += len;
}

/* ================================================================
 * Internal Helpers
 * ================================================================ */

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void format_addr(struct sockaddr_in *addr, char *out, size_t out_len)
{
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    snprintf(out, out_len, "%s:%d", ip, ntohs(addr->sin_port));
}

static void conn_reset(NetConn *c)
{
    c->fd = -1;
    c->state = NET_CONN_DISCONNECTED;
    c->last_cause = NET_CAUSE_NONE;
    ringbuf_init(&c->send_buf);
    ringbuf_init(&c->recv_buf);
    c->remote_addr[0] = '\0';
    c->user_data = NULL;
}

static void conn_close_fd(NetConn *c, NetDisconnectCause cause)
{
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    c->state = NET_CONN_DISCONNECTED;
    c->last_cause = cause;
    ringbuf_init(&c->send_buf);
    ringbuf_init(&c->recv_buf);
}

static int find_free_slot(const NetSocket *ns)
{
    for (int i = 0; i < ns->max_conns; i++) {
        if (ns->conns[i].state == NET_CONN_DISCONNECTED)
            return i;
    }
    return -1;
}

/* ================================================================
 * Lifecycle
 * ================================================================ */

int net_socket_init(NetSocket *ns, int max_connections)
{
    memset(ns, 0, sizeof(*ns));
    ns->listen_fd = -1;
    ns->max_conns = max_connections;

    ns->conns = calloc((size_t)max_connections, sizeof(NetConn));
    if (!ns->conns)
        return -1;

    /* +1 for listener at index 0 */
    ns->pollfds = calloc((size_t)(max_connections + 1), sizeof(struct pollfd));
    if (!ns->pollfds) {
        free(ns->conns);
        ns->conns = NULL;
        return -1;
    }

    for (int i = 0; i < max_connections; i++)
        conn_reset(&ns->conns[i]);

    /* Listener slot inactive by default */
    ns->pollfds[0].fd = -1;
    ns->pollfds[0].events = 0;

    for (int i = 0; i < max_connections; i++) {
        ns->pollfds[i + 1].fd = -1;
        ns->pollfds[i + 1].events = 0;
    }

    return 0;
}

void net_socket_shutdown(NetSocket *ns)
{
    if (ns->conns) {
        for (int i = 0; i < ns->max_conns; i++) {
            if (ns->conns[i].fd >= 0)
                close(ns->conns[i].fd);
        }
        free(ns->conns);
        ns->conns = NULL;
    }
    if (ns->listen_fd >= 0) {
        close(ns->listen_fd);
        ns->listen_fd = -1;
    }
    if (ns->pollfds) {
        free(ns->pollfds);
        ns->pollfds = NULL;
    }
    ns->max_conns = 0;
}

/* ================================================================
 * Server
 * ================================================================ */

int net_socket_listen(NetSocket *ns, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 16) < 0) {
        close(fd);
        return -1;
    }

    if (set_nonblocking(fd) < 0) {
        close(fd);
        return -1;
    }

    ns->listen_fd = fd;
    return 0;
}

int net_socket_accept(NetSocket *ns)
{
    if (ns->listen_fd < 0)
        return -1;

    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    int fd = accept(ns->listen_fd, (struct sockaddr *)&peer_addr, &peer_len);
    if (fd < 0)
        return -1;

    int slot = find_free_slot(ns);
    if (slot < 0) {
        close(fd);
        return -1;
    }

    if (set_nonblocking(fd) < 0) {
        close(fd);
        return -1;
    }

    NetConn *c = &ns->conns[slot];
    conn_reset(c);
    c->fd = fd;
    c->state = NET_CONN_CONNECTED;
    format_addr(&peer_addr, c->remote_addr, sizeof(c->remote_addr));

    return slot;
}

/* ================================================================
 * Client
 * ================================================================ */

int net_socket_connect(NetSocket *ns, const char *ip, uint16_t port)
{
    int slot = find_free_slot(ns);
    if (slot < 0)
        return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1)
        return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    if (set_nonblocking(fd) < 0) {
        close(fd);
        return -1;
    }

    NetConn *c = &ns->conns[slot];
    conn_reset(c);
    c->fd = fd;
    snprintf(c->remote_addr, sizeof(c->remote_addr), "%s:%u", ip, port);

    int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == 0) {
        /* Connected immediately (rare for TCP) */
        c->state = NET_CONN_CONNECTED;
    } else if (errno == EINPROGRESS) {
        c->state = NET_CONN_CONNECTING;
    } else {
        close(fd);
        conn_reset(c);
        return -1;
    }

    return slot;
}

/* ================================================================
 * Internal Poll Handlers
 * ================================================================ */

static void handle_connect_complete(NetConn *c)
{
    int err = 0;
    socklen_t errlen = sizeof(err);
    if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0 ||
        err != 0) {
        conn_close_fd(c, NET_CAUSE_ERROR);
        return;
    }
    c->state = NET_CONN_CONNECTED;
}

static void handle_recv(NetConn *c)
{
    size_t space = ringbuf_write_space(&c->recv_buf);
    if (space == 0)
        return; /* buffer full — will process next frame */

    /* First segment: tail to end of buffer */
    size_t tail_off = c->recv_buf.tail & NET_RINGBUF_MASK;
    size_t seg1 = NET_RINGBUF_CAPACITY - tail_off;
    if (seg1 > space)
        seg1 = space;

    ssize_t n = recv(c->fd, c->recv_buf.data + tail_off, seg1, 0);
    if (n > 0) {
        c->recv_buf.tail += (size_t)n;
        space -= (size_t)n;

        /* Second segment: wrap around to buffer start */
        if ((size_t)n == seg1 && space > 0) {
            size_t seg2 = space;
            ssize_t n2 = recv(c->fd, c->recv_buf.data, seg2, 0);
            if (n2 > 0)
                c->recv_buf.tail += (size_t)n2;
        }
    } else if (n == 0) {
        /* Peer closed */
        conn_close_fd(c, NET_CAUSE_CLOSED_REMOTE);
    } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            conn_close_fd(c, NET_CAUSE_ERROR);
    }
}

static void handle_send(NetConn *c)
{
    size_t avail = ringbuf_read_avail(&c->send_buf);
    if (avail == 0)
        return;

    /* First segment: head to end of buffer */
    size_t head_off = c->send_buf.head & NET_RINGBUF_MASK;
    size_t seg1 = NET_RINGBUF_CAPACITY - head_off;
    if (seg1 > avail)
        seg1 = avail;

    ssize_t n = send(c->fd, c->send_buf.data + head_off, seg1, MSG_NOSIGNAL);
    if (n > 0) {
        c->send_buf.head += (size_t)n;
        avail -= (size_t)n;

        /* Second segment: wrap around */
        if ((size_t)n == seg1 && avail > 0) {
            size_t seg2 = avail;
            ssize_t n2 =
                send(c->fd, c->send_buf.data, seg2, MSG_NOSIGNAL);
            if (n2 > 0)
                c->send_buf.head += (size_t)n2;
        }
    } else if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            conn_close_fd(c, NET_CAUSE_ERROR);
    }
}

/* ================================================================
 * Per-Frame Update
 * ================================================================ */

void net_socket_update(NetSocket *ns)
{
    /* Build pollfd array */
    ns->pollfds[0].fd = ns->listen_fd;
    ns->pollfds[0].events = (ns->listen_fd >= 0) ? POLLIN : 0;
    ns->pollfds[0].revents = 0;

    for (int i = 0; i < ns->max_conns; i++) {
        NetConn *c = &ns->conns[i];
        struct pollfd *pf = &ns->pollfds[i + 1];
        pf->revents = 0;

        switch (c->state) {
        case NET_CONN_DISCONNECTED:
            pf->fd = -1;
            pf->events = 0;
            break;
        case NET_CONN_CONNECTING:
            pf->fd = c->fd;
            pf->events = POLLOUT;
            break;
        case NET_CONN_CONNECTED:
            pf->fd = c->fd;
            pf->events = POLLIN;
            if (ringbuf_read_avail(&c->send_buf) > 0)
                pf->events |= POLLOUT;
            break;
        case NET_CONN_CLOSING:
            pf->fd = c->fd;
            if (ringbuf_read_avail(&c->send_buf) > 0)
                pf->events = POLLOUT;
            else
                pf->events = 0;
            break;
        }
    }

    int nfds = ns->max_conns + 1;
    int ret = poll(ns->pollfds, (nfds_t)nfds, 0);
    if (ret <= 0)
        return;

    /* Accept new connections */
    if (ns->pollfds[0].revents & POLLIN)
        net_socket_accept(ns);

    /* Process each connection */
    for (int i = 0; i < ns->max_conns; i++) {
        NetConn *c = &ns->conns[i];
        struct pollfd *pf = &ns->pollfds[i + 1];

        if (pf->fd < 0 || pf->revents == 0)
            continue;

        /* Error / hangup */
        if (pf->revents & (POLLERR | POLLNVAL)) {
            conn_close_fd(c, NET_CAUSE_ERROR);
            continue;
        }

        switch (c->state) {
        case NET_CONN_CONNECTING:
            if (pf->revents & POLLOUT)
                handle_connect_complete(c);
            break;

        case NET_CONN_CONNECTED:
            if (pf->revents & POLLIN)
                handle_recv(c);
            if (c->state == NET_CONN_CONNECTED && (pf->revents & POLLOUT))
                handle_send(c);
            break;

        case NET_CONN_CLOSING:
            if (pf->revents & POLLOUT)
                handle_send(c);
            /* If send buffer drained, finish close */
            if (ringbuf_read_avail(&c->send_buf) == 0) {
                shutdown(c->fd, SHUT_WR);
                conn_close_fd(c, NET_CAUSE_CLOSED_LOCAL);
            }
            break;

        case NET_CONN_DISCONNECTED:
            break;
        }

        /* Handle POLLHUP after processing (peer may have sent data + HUP).
         * If we initiated the close (CLOSING), don't overwrite the cause. */
        if (c->state != NET_CONN_DISCONNECTED &&
            c->state != NET_CONN_CLOSING && (pf->revents & POLLHUP)) {
            conn_close_fd(c, NET_CAUSE_CLOSED_REMOTE);
        } else if (c->state == NET_CONN_CLOSING && (pf->revents & POLLHUP)) {
            conn_close_fd(c, NET_CAUSE_CLOSED_LOCAL);
        }
    }
}

/* ================================================================
 * Send / Receive
 * ================================================================ */

int net_socket_send(NetSocket *ns, int conn_id, const uint8_t *data,
                    size_t len)
{
    if (conn_id < 0 || conn_id >= ns->max_conns)
        return -1;
    NetConn *c = &ns->conns[conn_id];
    if (c->state != NET_CONN_CONNECTED && c->state != NET_CONN_CLOSING)
        return -1;
    if (ringbuf_write_space(&c->send_buf) < len)
        return -1;
    ringbuf_write(&c->send_buf, data, len);
    return 0;
}

int net_socket_recv(NetSocket *ns, int conn_id, uint8_t *out, size_t max_len,
                    size_t *out_len)
{
    if (conn_id < 0 || conn_id >= ns->max_conns)
        return -1;
    NetConn *c = &ns->conns[conn_id];
    if (c->state == NET_CONN_DISCONNECTED)
        return -1;
    *out_len = ringbuf_peek(&c->recv_buf, out, max_len);
    return 0;
}

void net_socket_recv_consume(NetSocket *ns, int conn_id, size_t len)
{
    if (conn_id < 0 || conn_id >= ns->max_conns)
        return;
    ringbuf_consume(&ns->conns[conn_id].recv_buf, len);
}

/* ================================================================
 * Connection Management
 * ================================================================ */

void net_socket_close(NetSocket *ns, int conn_id)
{
    if (conn_id < 0 || conn_id >= ns->max_conns)
        return;
    NetConn *c = &ns->conns[conn_id];
    if (c->state == NET_CONN_DISCONNECTED)
        return;

    if (ringbuf_read_avail(&c->send_buf) > 0) {
        c->state = NET_CONN_CLOSING;
    } else {
        shutdown(c->fd, SHUT_WR);
        conn_close_fd(c, NET_CAUSE_CLOSED_LOCAL);
    }
}

NetConnState net_socket_state(const NetSocket *ns, int conn_id)
{
    if (conn_id < 0 || conn_id >= ns->max_conns)
        return NET_CONN_DISCONNECTED;
    return ns->conns[conn_id].state;
}

int net_socket_count(const NetSocket *ns)
{
    int count = 0;
    for (int i = 0; i < ns->max_conns; i++) {
        if (ns->conns[i].state != NET_CONN_DISCONNECTED)
            count++;
    }
    return count;
}

/* ================================================================
 * Protocol Integration
 * ================================================================ */

int net_socket_send_msg(NetSocket *ns, int conn_id, const NetMsg *msg)
{
    uint8_t buf[NET_FRAME_HEADER_SIZE + NET_MAX_MSG_SIZE];
    int len = net_msg_write_framed(msg, buf, sizeof(buf));
    if (len < 0)
        return -1;
    return net_socket_send(ns, conn_id, buf, (size_t)len);
}

bool net_socket_recv_msg(NetSocket *ns, int conn_id, NetMsg *msg)
{
    if (conn_id < 0 || conn_id >= ns->max_conns)
        return false;
    NetConn *c = &ns->conns[conn_id];

    size_t avail = ringbuf_read_avail(&c->recv_buf);
    if (avail < NET_FRAME_HEADER_SIZE)
        return false;

    /* Linearize recv buffer for frame parsing */
    uint8_t linear[NET_FRAME_HEADER_SIZE + NET_MAX_MSG_SIZE];
    size_t to_peek = avail < sizeof(linear) ? avail : sizeof(linear);
    ringbuf_peek(&c->recv_buf, linear, to_peek);

    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    int frame_bytes = net_frame_read(linear, to_peek, &payload, &payload_len);

    if (frame_bytes <= 0)
        return false; /* incomplete or error */

    int deser = net_msg_deserialize(msg, payload, payload_len);
    if (deser < 0) {
        /* Malformed message — consume the frame and discard */
        ringbuf_consume(&c->recv_buf, (size_t)frame_bytes);
        return false;
    }

    ringbuf_consume(&c->recv_buf, (size_t)frame_bytes);
    return true;
}
