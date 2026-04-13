CC = gcc
CFLAGS = -Wall -Wextra -O2 -I../include

EXEC = tetris_ws
SRCS = ws_tetris.c ../src/string.c ../src/math.c ../src/memory.c ../src/screen.c ../src/keyboard.c

all: $(EXEC)

$(EXEC): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(EXEC)

.PHONY: all clean
