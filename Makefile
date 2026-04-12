CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200112L -Isrc -MMD -MP

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
                  src/core/settings.c src/core/resource.c \
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
all: $(CLIENT_BIN) $(SERVER_BIN) $(LOBBY_BIN)

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
# Debug builds
# ================================================================

debug:
	$(MAKE) clean
	$(MAKE) $(CLIENT_BIN) CFLAGS="$(CFLAGS) -DDEBUG -g -O0"

debug-server:
	$(MAKE) clean
	$(MAKE) $(SERVER_BIN) CFLAGS="$(CFLAGS) -DDEBUG -g -O0"

debug-all:
	$(MAKE) clean
	$(MAKE) $(CLIENT_BIN) $(SERVER_BIN) $(LOBBY_BIN) CFLAGS="$(CFLAGS) -DDEBUG -g -O0"

# ================================================================
# Clean
# ================================================================

clean:
	rm -f $(ALL_OBJ) $(ALL_DEP) $(CLIENT_BIN) $(SERVER_BIN) $(LOBBY_BIN)

clean-dist:
	rm -rf build/dist-obj

-include $(ALL_DEP)

# ================================================================
# Distribution builds (single-file binary with embedded assets)
# ================================================================

RAYLIB_DIR     = vendor/raylib/src
RAYLIB_A_LINUX = vendor/raylib/build-linux/libraylib.a
RAYLIB_A_WIN   = vendor/raylib/build-win/libraylib.a
EMBED_DIR      = build/embedded
DIST_OBJ_DIR   = build/dist-obj

# Collect dist source files: client sources + embedded asset .c files
DIST_SRC     = $(CLIENT_SRC)
DIST_EMBED_C = $(filter-out $(EMBED_DIR)/bin2c.c,$(wildcard $(EMBED_DIR)/*.c))

# Convert source paths to dist object paths under build/dist-obj/
DIST_OBJ     = $(patsubst %.c,$(DIST_OBJ_DIR)/%.o,$(DIST_SRC))
DIST_EMBED_O = $(patsubst %.c,$(DIST_OBJ_DIR)/%.o,$(DIST_EMBED_C))

DIST_CFLAGS_BASE = -Wall -Wextra -std=c11 -O2 -Isrc -I$(EMBED_DIR) -Ivendor/raylib/src -DHH_EMBEDDED -MMD -MP
DIST_CFLAGS  = $(DIST_CFLAGS_BASE) -D_POSIX_C_SOURCE=200112L

# ---- Embed assets ----
.PHONY: embed
embed:
	./scripts/embed_assets.sh $(EMBED_DIR)

# ---- Build raylib static (Linux) ----
$(RAYLIB_A_LINUX):
	mkdir -p vendor/raylib/build-linux
	cd $(RAYLIB_DIR) && $(MAKE) clean && \
		$(MAKE) PLATFORM=PLATFORM_DESKTOP RAYLIB_BUILD_MODE=RELEASE \
		GLFW_LINUX_ENABLE_X11=TRUE GLFW_LINUX_ENABLE_WAYLAND=TRUE
	cp $(RAYLIB_DIR)/libraylib.a $(RAYLIB_A_LINUX)

# ---- Build raylib static (Windows/MinGW) ----
$(RAYLIB_A_WIN):
	mkdir -p vendor/raylib/build-win
	cd $(RAYLIB_DIR) && $(MAKE) clean && \
		$(MAKE) CC=x86_64-w64-mingw32-gcc AR=x86_64-w64-mingw32-ar \
		PLATFORM=PLATFORM_DESKTOP OS=Windows_NT RAYLIB_BUILD_MODE=RELEASE
	cp $(RAYLIB_DIR)/libraylib.a $(RAYLIB_A_WIN)

# ---- Dist compile rule (separate obj dir to avoid conflicts with dev builds) ----
$(DIST_OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(DIST_CFLAGS) -c $< -o $@

# ---- Linux distribution binary ----
.PHONY: dist-linux
dist-linux: embed $(RAYLIB_A_LINUX)
	rm -rf $(DIST_OBJ_DIR)
	$(MAKE) linux-hearts-dist

linux-hearts-dist: $(DIST_OBJ) $(DIST_EMBED_O)
	$(CC) $^ -o linux-hearts -s $(RAYLIB_A_LINUX) -lm -lGL -lpthread -ldl -lrt -lX11 \
		$(shell pkg-config wayland-client wayland-cursor wayland-egl xkbcommon --libs)
	@echo "==> Built: linux-hearts (Linux distribution, $(shell du -h linux-hearts | cut -f1))"

# ---- Windows distribution binary ----
.PHONY: dist-windows
dist-windows: embed $(RAYLIB_A_WIN)
	rm -rf $(DIST_OBJ_DIR)
	$(MAKE) hollow-hearts-win CC=x86_64-w64-mingw32-gcc DIST_CFLAGS="$(DIST_CFLAGS_BASE)"

hollow-hearts-win: $(DIST_OBJ) $(DIST_EMBED_O)
	$(CC) $^ -o hollow-hearts.exe -s -static -mwindows $(RAYLIB_A_WIN) -lopengl32 -lgdi32 -lwinmm -lws2_32 -lbcrypt -lshell32 -lm
	@echo "==> Built: hollow-hearts.exe (Windows distribution, $(shell du -h hollow-hearts.exe | cut -f1))"

.PHONY: all server lobby debug debug-server debug-all clean clean-dist embed dist-linux dist-windows linux-hearts-dist hollow-hearts-win
