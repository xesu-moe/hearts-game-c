/* ============================================================
 * @deps-implements: game server entry point
 * @deps-requires: server/server_net.h (server_net_init, server_net_shutdown,
 *                 server_net_listen, server_net_update),
 *                 server/room.h (MAX_ROOMS, NET_MAX_PLAYERS),
 *                 server/lobby_link.h (lobby_link_init, lobby_link_connect,
 *                 lobby_link_update, lobby_link_shutdown),
 *                 core/clock.h (FIXED_DT, MAX_FRAME_DT, MAX_CATCHUP),
 *                 stdio.h, stdlib.h, signal.h, time.h, string.h
 * @deps-last-changed: 2026-03-24 — Step 16: Lobby link integration
 * ============================================================ */

#define _POSIX_C_SOURCE 199309L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "server_net.h"
#include "room.h"
#include "lobby_link.h"
#include "core/clock.h" /* FIXED_DT, MAX_FRAME_DT, MAX_CATCHUP */

#define DEFAULT_PORT 7777

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* Returns current time in seconds (monotonic clock) */
static double time_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Sleep for the given number of seconds */
static void time_sleep(double seconds)
{
    if (seconds <= 0.0) return;
    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - (double)req.tv_sec) * 1e9);
    nanosleep(&req, NULL);
}

int main(int argc, char *argv[])
{
    /* Parse CLI: hh-server [port] [lobby_addr lobby_port] */
    uint16_t port = DEFAULT_PORT;
    const char *lobby_addr = NULL;
    uint16_t lobby_port = 0;
    bool use_lobby = false;

    if (argc >= 2) {
        int p = atoi(argv[1]);
        if (p <= 0 || p > 65535) {
            fprintf(stderr, "Usage: %s [port] [lobby_addr lobby_port]\n", argv[0]);
            fprintf(stderr, "  port:       1-65535 (default: %d)\n", DEFAULT_PORT);
            fprintf(stderr, "  lobby_addr: lobby server IP (optional)\n");
            fprintf(stderr, "  lobby_port: lobby server port (optional)\n");
            return 1;
        }
        port = (uint16_t)p;
    }
    if (argc >= 4) {
        lobby_addr = argv[2];
        int lp = atoi(argv[3]);
        if (lp <= 0 || lp > 65535) {
            fprintf(stderr, "Invalid lobby port: %s\n", argv[3]);
            return 1;
        }
        lobby_port = (uint16_t)lp;
        use_lobby = true;
    }

    /* Signal handling for graceful shutdown */
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Seed RNG */
    srand((unsigned)time(NULL));

    printf("hh-server starting...\n");

    /* Initialize network layer and start listening */
    server_net_init(MAX_ROOMS * NET_MAX_PLAYERS + 16);
    if (!server_net_listen(port)) {
        fprintf(stderr, "Failed to listen on port %d\n", port);
        return 1;
    }

    /* Initialize lobby link if configured */
    if (use_lobby) {
        lobby_link_init(lobby_addr, lobby_port, "127.0.0.1", port, MAX_ROOMS);
        lobby_link_connect();
    }

    /* Fixed-timestep loop — server runs indefinitely */
    double last_time = time_now();
    double accumulator = 0.0;

    while (g_running) {
        double now = time_now();
        double elapsed = now - last_time;
        last_time = now;

        /* Clamp to prevent spiral of death */
        if (elapsed > (double)MAX_FRAME_DT) {
            elapsed = (double)MAX_FRAME_DT;
        }
        accumulator += elapsed;

        /* Process fixed-timestep ticks */
        int ticks = 0;
        while (accumulator >= (double)FIXED_DT && ticks < MAX_CATCHUP) {
            server_net_update();
            if (use_lobby) lobby_link_update();
            accumulator -= (double)FIXED_DT;
            ticks++;
        }

        /* Sleep until next tick to avoid busy-waiting */
        double sleep_time = (double)FIXED_DT - accumulator;
        if (sleep_time > 0.001) {
            time_sleep(sleep_time);
        }
    }

    printf("\nServer interrupted (SIGINT/SIGTERM)\n");
    if (use_lobby) lobby_link_shutdown();
    server_net_shutdown();
    printf("hh-server shutting down.\n");
    return 0;
}
