CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Isrc -MMD -MP
LDFLAGS = -lraylib -lm

SRC = $(wildcard src/*.c) $(wildcard src/core/*.c) $(wildcard src/render/*.c) $(wildcard src/phase2/*.c) $(wildcard src/vendor/*.c) $(wildcard src/game/*.c)
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)
BIN = hollow-hearts

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(DEP) $(BIN)

-include $(DEP)

.PHONY: all clean
