CC = gcc
CFLAGS = -Wall -Wextra -O2

EXEC = tetris_ws
SRCS = ws_tetris.c

all: $(EXEC)

$(EXEC): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(EXEC)

.PHONY: all clean
