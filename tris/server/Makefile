CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g -Iinclude
SRCS    = src/main.c src/state.c src/match.c src/net.c src/protocol.c
OBJS    = $(SRCS:.c=.o)
TARGET  = server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean