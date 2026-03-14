CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Isrc
LDFLAGS = -lraylib -lm

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
BIN = hollow-hearts

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
