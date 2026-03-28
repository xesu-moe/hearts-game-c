#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#ifdef DEBUG

#include <stdio.h>
#include <stdlib.h>

/* Subsystem bitmask for filtering */
#define DBG_QUEUE    0x01
#define DBG_STATE    0x02
#define DBG_FLOW     0x04
#define DBG_ANIM     0x08
#define DBG_SYNC     0x10
#define DBG_DETECT   0x20
#define DBG_CMD      0x40
#define DBG_SERVER   0x80
#define DBG_ALL      0xFF

/* Globals — defined in main.c (client) or server_main.c (server) */
extern unsigned g_dbg_mask;
extern unsigned g_dbg_frame;

static inline const char *dbg_subsystem_name(unsigned s) {
    switch (s) {
    case DBG_QUEUE:  return "QUEUE";
    case DBG_STATE:  return "STATE";
    case DBG_FLOW:   return "FLOW";
    case DBG_ANIM:   return "ANIM";
    case DBG_SYNC:   return "SYNC";
    case DBG_DETECT: return "DETECT";
    case DBG_CMD:    return "CMD";
    case DBG_SERVER: return "SERVER";
    default:         return "???";
    }
}

#define DBG(subsystem, fmt, ...) do { \
    if (g_dbg_mask & (subsystem)) \
        fprintf(stderr, "[%s F%u] " fmt "\n", \
                dbg_subsystem_name(subsystem), g_dbg_frame, ##__VA_ARGS__); \
} while (0)

static inline void dbg_init_from_env(void) {
    const char *env = getenv("HH_DBG");
    if (env) g_dbg_mask = (unsigned)strtol(env, NULL, 0);
}

#else

#define DBG(subsystem, fmt, ...) ((void)0)
static inline void dbg_init_from_env(void) {}

#endif /* DEBUG */

#endif /* DEBUG_LOG_H */
