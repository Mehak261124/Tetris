CC     = gcc
CFLAGS = -Wall -Wextra -Iinclude

SRCS = src/main.c src/math.c src/string.c src/memory.c src/screen.c src/keyboard.c
OBJS = $(SRCS:.c=.o)
EXEC = tetris_os

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(EXEC)

.PHONY: all clean
