CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g
SRCS    = src/client.c
OBJS    = $(SRCS:.c=.o)
TARGET  = client

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean