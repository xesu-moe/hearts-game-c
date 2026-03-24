#ifndef SOCKET_H
#define SOCKET_H

/* ============================================================
 * Non-blocking TCP socket abstraction with poll()-based
 * multiplexing and per-connection ring buffers.
 *
 * Single-threaded design. No DNS — accepts IP addresses only.
 * Used by client, game server, and lobby server.
 *
 * @deps-exports: NetRingBuf, NetConnState, NetDisconnectCause, NetConn,
 *                NetSocket, net_socket_init/shutdown/listen/accept/connect/
 *                update/send/recv/recv_consume/close/state/count/
 *                send_msg/recv_msg
 * @deps-requires: net/protocol.h (NetMsg, NET_ADDR_LEN)
 * @deps-used-by: client_net.c, server_net.c, lobby_net.c
 * @deps-last-changed: 2026-03-22 — Initial creation
 * ============================================================ */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "net/protocol.h"

/* ================================================================
 * Ring Buffer
 * ================================================================ */

#define NET_RINGBUF_CAPACITY 16384 /* 16KB, must be power of 2 */
#define NET_RINGBUF_MASK     (NET_RINGBUF_CAPACITY - 1)

typedef struct NetRingBuf {
    uint8_t  data[NET_RINGBUF_CAPACITY];
    uint64_t head; /* read position (monotonically increasing, never wraps) */
    uint64_t tail; /* write position (monotonically increasing, never wraps) */
} NetRingBuf;

/* ================================================================
 * Connection State
 * ================================================================ */

typedef enum NetConnState {
    NET_CONN_DISCONNECTED = 0,
    NET_CONN_CONNECTING,
    NET_CONN_CONNECTED,
    NET_CONN_CLOSING
} NetConnState;

typedef enum NetDisconnectCause {
    NET_CAUSE_NONE = 0,
    NET_CAUSE_CLOSED_LOCAL,  /* we initiated close */
    NET_CAUSE_CLOSED_REMOTE, /* peer closed (recv returned 0) */
    NET_CAUSE_ERROR          /* ECONNRESET, EPIPE, etc. */
} NetDisconnectCause;

/* ================================================================
 * Connection
 * ================================================================ */

typedef struct NetConn {
    int                fd;    /* socket fd, -1 if unused */
    NetConnState       state;
    NetDisconnectCause last_cause;
    NetRingBuf         send_buf;
    NetRingBuf         recv_buf;
    char               remote_addr[NET_ADDR_LEN]; /* "1.2.3.4:5678" */
    void              *user_data; /* server maps conn → room/player */
} NetConn;

/* ================================================================
 * Socket Manager
 * ================================================================ */

typedef struct NetSocket {
    NetConn        *conns;    /* dynamically allocated [0..max_conns) */
    struct pollfd  *pollfds;  /* idx 0=listener, idx i+1=conns[i] */
    int             max_conns;
    int             listen_fd; /* server listener, -1 for client */
} NetSocket;

/* ================================================================
 * Lifecycle
 * ================================================================ */

/* Allocate connection and pollfd arrays. Returns 0 or -1. */
int net_socket_init(NetSocket *ns, int max_connections);

/* Close all connections, free memory. */
void net_socket_shutdown(NetSocket *ns);

/* ================================================================
 * Server
 * ================================================================ */

/* Bind to INADDR_ANY:port, listen, set non-blocking. Returns 0 or -1. */
int net_socket_listen(NetSocket *ns, uint16_t port);

/* Accept a pending connection. Returns conn index or -1. */
int net_socket_accept(NetSocket *ns);

/* ================================================================
 * Client
 * ================================================================ */

/* Non-blocking connect to ip:port (IP address only, no DNS).
 * State transitions to CONNECTING. Returns conn index or -1. */
int net_socket_connect(NetSocket *ns, const char *ip, uint16_t port);

/* ================================================================
 * Per-Frame Update
 * ================================================================ */

/* Poll all sockets, accept new connections, complete async connects,
 * read into recv buffers, flush send buffers, detect disconnections.
 * Call once per frame from the main loop. */
void net_socket_update(NetSocket *ns);

/* ================================================================
 * Send / Receive
 * ================================================================ */

/* Enqueue data into connection's send buffer.
 * Returns 0 on success, -1 if buffer full or connection invalid. */
int net_socket_send(NetSocket *ns, int conn_id,
                    const uint8_t *data, size_t len);

/* Peek at connection's recv buffer. Copies up to max_len bytes into out,
 * sets *out_len to actual bytes copied. Does NOT consume.
 * Returns 0 on success, -1 if connection invalid. */
int net_socket_recv(NetSocket *ns, int conn_id,
                    uint8_t *out, size_t max_len, size_t *out_len);

/* Advance recv buffer read pointer by len bytes. */
void net_socket_recv_consume(NetSocket *ns, int conn_id, size_t len);

/* ================================================================
 * Connection Management
 * ================================================================ */

/* Initiate graceful close. Drains send buffer first if non-empty. */
void net_socket_close(NetSocket *ns, int conn_id);

/* Query connection state. */
NetConnState net_socket_state(const NetSocket *ns, int conn_id);

/* Count active (non-DISCONNECTED) connections. */
int net_socket_count(const NetSocket *ns);

/* ================================================================
 * Protocol Integration (convenience)
 * ================================================================ */

/* Serialize + frame + enqueue a NetMsg to send buffer.
 * Returns 0 on success, -1 on error. */
int net_socket_send_msg(NetSocket *ns, int conn_id, const NetMsg *msg);

/* Try to extract one complete framed message from recv buffer.
 * Returns true if a message was extracted, false if incomplete.
 * Caller should loop until false to drain all buffered messages. */
bool net_socket_recv_msg(NetSocket *ns, int conn_id, NetMsg *msg);

#endif /* SOCKET_H */
