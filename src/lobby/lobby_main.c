/* ============================================================
 * Lobby Server Entry Point (hh-lobby)
 *
 * Headless server: SQLite database + TCP listener.
 * Handles player accounts, room codes, matchmaking, and
 * game server registry.
 *
 * Usage: hh-lobby [port] [db_path]
 *
 * @deps-implements: lobby server entry point
 * @deps-requires: lobby/db.h (LobbyDB, lobbydb_open, lobbydb_close),
 *                 lobby/lobby_net.h (lobby_net_init, lobby_net_shutdown,
 *                 lobby_net_listen, lobby_net_update),
 *                 signal.h, stdio.h, stdlib.h, time.h
 * @deps-last-changed: 2026-03-24 — Step 14: Lobby Server Foundation
 * ============================================================ */

#define _POSIX_C_SOURCE 199309L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "db.h"
#include "lobby_net.h"

/* ================================================================
 * Constants
 * ================================================================ */

#define DEFAULT_PORT          9999
#define DEFAULT_DB_PATH       "lobby.db"
#define MAX_LOBBY_CONNECTIONS 256
#define LOBBY_TICK_DT         (1.0 / 30.0) /* 30Hz tick rate */
#define LOBBY_MAX_FRAME_DT    0.5
#define LOBBY_MAX_CATCHUP     3

/* ================================================================
 * Signal handling
 * ================================================================ */

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ================================================================
 * Time helpers (same pattern as server_main.c)
 * ================================================================ */

static double time_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void time_sleep(double seconds)
{
    if (seconds <= 0.0) return;
    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - (double)req.tv_sec) * 1e9);
    nanosleep(&req, NULL);
}

/* ================================================================
 * Entry point
 * ================================================================ */

int main(int argc, char *argv[])
{
    /* Parse CLI: [port] [db_path] */
    uint16_t port = DEFAULT_PORT;
    const char *db_path = DEFAULT_DB_PATH;

    if (argc >= 2) {
        int p = atoi(argv[1]);
        if (p <= 0 || p > 65535) {
            fprintf(stderr, "Usage: %s [port] [db_path]\n", argv[0]);
            fprintf(stderr, "  port:    1-65535 (default: %d)\n", DEFAULT_PORT);
            fprintf(stderr, "  db_path: SQLite database path (default: %s)\n",
                    DEFAULT_DB_PATH);
            return 1;
        }
        port = (uint16_t)p;
    }
    if (argc >= 3) {
        db_path = argv[2];
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

    printf("hh-lobby starting...\n");

    /* Initialize database */
    LobbyDB ldb;
    if (!lobbydb_open(&ldb, db_path)) {
        fprintf(stderr, "Failed to open database: %s\n", db_path);
        return 1;
    }

    /* Initialize networking */
    lobby_net_init(MAX_LOBBY_CONNECTIONS, &ldb);
    if (!lobby_net_listen(port)) {
        fprintf(stderr, "Failed to listen on port %d\n", port);
        lobby_net_shutdown();
        lobbydb_close(&ldb);
        return 1;
    }

    printf("hh-lobby running on port %d (db: %s)\n", port, db_path);

    /* Fixed-timestep loop at 30Hz */
    double last_time = time_now();
    double accumulator = 0.0;

    while (g_running) {
        double now = time_now();
        double elapsed = now - last_time;
        last_time = now;

        /* Clamp to prevent spiral of death */
        if (elapsed > LOBBY_MAX_FRAME_DT) {
            elapsed = LOBBY_MAX_FRAME_DT;
        }
        accumulator += elapsed;

        /* Process fixed-timestep ticks */
        int ticks = 0;
        while (accumulator >= LOBBY_TICK_DT && ticks < LOBBY_MAX_CATCHUP) {
            lobby_net_update();
            accumulator -= LOBBY_TICK_DT;
            ticks++;
        }

        /* Sleep until next tick to avoid busy-waiting */
        double sleep_time = LOBBY_TICK_DT - accumulator;
        if (sleep_time > 0.001) {
            time_sleep(sleep_time);
        }
    }

    /* Shutdown (reverse init order) */
    printf("\nhh-lobby shutting down...\n");
    lobby_net_shutdown();
    lobbydb_close(&ldb);
    printf("hh-lobby stopped.\n");
    return 0;
}
