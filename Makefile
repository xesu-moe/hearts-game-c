CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Isrc -MMD -MP

# ================================================================
# Source lists
# ================================================================

# Shared by all binaries (Raylib-free)
CORE_SRC = src/core/card.c src/core/hand.c src/core/deck.c \
           src/core/trick.c src/core/player.c src/core/game_state.c \
           src/core/input_cmd.c
NET_SRC    = $(wildcard src/net/*.c)
P2_SRC     = $(wildcard src/phase2/*.c)
VENDOR_SRC = $(wildcard src/vendor/*.c)
SHARED_SRC = $(CORE_SRC) $(NET_SRC) $(P2_SRC) $(VENDOR_SRC)

# Client-only sources
CLIENT_ONLY_SRC = src/main.c src/core/input.c src/core/clock.c \
                  src/core/settings.c \
                  $(wildcard src/render/*.c) \
                  $(wildcard src/game/*.c) \
                  $(wildcard src/audio/*.c)

# Server-only sources
SERVER_ONLY_SRC = $(wildcard src/server/*.c)

# Lobby-only sources
LOBBY_ONLY_SRC = $(wildcard src/lobby/*.c)

# ================================================================
# Per-target source and object lists
# ================================================================

CLIENT_SRC = $(CLIENT_ONLY_SRC) $(SHARED_SRC)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)
CLIENT_DEP = $(CLIENT_SRC:.c=.d)
CLIENT_BIN = hollow-hearts
CLIENT_LDFLAGS = -lraylib -lm

SERVER_SRC = $(SERVER_ONLY_SRC) $(SHARED_SRC)
SERVER_OBJ = $(SERVER_SRC:.c=.o)
SERVER_DEP = $(SERVER_SRC:.c=.d)
SERVER_BIN = hh-server
SERVER_LDFLAGS = -lm

LOBBY_NET_SRC = src/net/protocol.c src/net/socket.c
LOBBY_SRC = $(LOBBY_ONLY_SRC) $(LOBBY_NET_SRC) $(VENDOR_SRC)
LOBBY_OBJ = $(LOBBY_SRC:.c=.o)
LOBBY_DEP = $(LOBBY_SRC:.c=.d)
LOBBY_BIN = hh-lobby
LOBBY_LDFLAGS = -lm -lsqlite3

ALL_OBJ = $(sort $(CLIENT_OBJ) $(SERVER_OBJ) $(LOBBY_OBJ))
ALL_DEP = $(sort $(CLIENT_DEP) $(SERVER_DEP) $(LOBBY_DEP))

# ================================================================
# Targets
# ================================================================

# Default: build client only
all: $(CLIENT_BIN)

$(CLIENT_BIN): $(CLIENT_OBJ)
	$(CC) $(CLIENT_OBJ) -o $@ $(CLIENT_LDFLAGS)

server: $(SERVER_BIN)

$(SERVER_BIN): $(SERVER_OBJ)
	$(CC) $(SERVER_OBJ) -o $@ $(SERVER_LDFLAGS)

lobby: $(LOBBY_BIN)

$(LOBBY_BIN): $(LOBBY_OBJ)
	$(CC) $(LOBBY_OBJ) -o $@ $(LOBBY_LDFLAGS)

# ================================================================
# Compile rule
# ================================================================

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ================================================================
# Debug build (client only)
# ================================================================

debug:
	$(MAKE) clean
	$(MAKE) $(CLIENT_BIN) CFLAGS="$(CFLAGS) -DDEBUG -g -O0"

# ================================================================
# Clean
# ================================================================

clean:
	rm -f $(ALL_OBJ) $(ALL_DEP) $(CLIENT_BIN) $(SERVER_BIN) $(LOBBY_BIN)

-include $(ALL_DEP)

.PHONY: all server lobby debug clean
