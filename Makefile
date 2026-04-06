CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c99
PTHREAD_FLAGS = -pthread
PROG = cpptools-proxy

.PHONY: all clean

all: $(PROG)

$(PROG): cpptools-proxy.c
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) -o $(PROG) cpptools-proxy.c

clean:
	rm -f $(PROG)

.PHONY: clean
