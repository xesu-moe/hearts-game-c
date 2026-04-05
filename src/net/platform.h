/* ============================================================
 * platform.h — Cross-platform socket and OS compatibility layer.
 *
 * Include this instead of sys/socket.h, netinet/in.h, arpa/inet.h,
 * poll.h, fcntl.h, unistd.h in networking code.
 * ============================================================ */
#ifndef NET_PLATFORM_H
#define NET_PLATFORM_H

#include <stdint.h>

#ifdef _WIN32

/* Windows — Winsock2 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

/* Undo Win32 API name conflicts with Raylib */
#ifdef CloseWindow
#undef CloseWindow
#endif
#ifdef ShowCursor
#undef ShowCursor
#endif
#ifdef DrawText
#undef DrawText
#endif
#ifdef DrawTextEx
#undef DrawTextEx
#endif
#ifdef LoadImage
#undef LoadImage
#endif
#ifdef Rectangle
#undef Rectangle
#endif

#include <io.h>
#include <direct.h>

/* Winsock uses SOCKET (unsigned), POSIX uses int fd. */
typedef SOCKET net_fd_t;
#define NET_INVALID_FD INVALID_SOCKET

/* Winsock close / poll / ioctl */
#define net_close_fd(fd)    closesocket(fd)
#define net_poll(fds, n, t) WSAPoll(fds, (ULONG)(n), t)

/* MSG_NOSIGNAL doesn't exist on Windows (no SIGPIPE) */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* POSIX errno codes used in socket.c — remap to Winsock equivalents */
#undef EAGAIN
#define EAGAIN      WSAEWOULDBLOCK
#undef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#undef EINPROGRESS
#define EINPROGRESS WSAEWOULDBLOCK

/* Replace errno with WSAGetLastError() for socket ops */
#define net_socket_errno() WSAGetLastError()

/* Shutdown constants */
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH

/* Non-blocking mode */
static inline int net_set_nonblocking(net_fd_t fd)
{
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
}

/* Monotonic time in milliseconds */
static inline uint32_t net_get_monotonic_ms(void)
{
    ULONGLONG tick = GetTickCount64();
    return (uint32_t)tick;
}

/* Directory creation */
#define net_mkdir(path, mode) _mkdir(path)

/* WSA init/cleanup */
static inline int net_platform_init(void)
{
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa);
}

static inline void net_platform_cleanup(void)
{
    WSACleanup();
}

/* setsockopt on Windows takes (const char*) not (const void*) */
#define net_setsockopt(fd, level, opt, val, len) \
    setsockopt(fd, level, opt, (const char *)(val), len)

#else /* POSIX */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef int net_fd_t;
#define NET_INVALID_FD (-1)

#define net_close_fd(fd)    close(fd)
#define net_poll(fds, n, t) poll(fds, (nfds_t)(n), t)
#define net_socket_errno()  errno

static inline int net_set_nonblocking(net_fd_t fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static inline uint32_t net_get_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#define net_mkdir(path, mode) mkdir(path, mode)

static inline int net_platform_init(void) { return 0; }
static inline void net_platform_cleanup(void) { (void)0; }

#define net_setsockopt(fd, level, opt, val, len) \
    setsockopt(fd, level, opt, val, len)

#endif /* _WIN32 */

#endif /* NET_PLATFORM_H */
