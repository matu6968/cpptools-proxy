CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c99
ifeq ($(OS),Windows_NT)
PTHREAD_FLAGS =
else
PTHREAD_FLAGS = -pthread
endif
PROG = cpptools-proxy

.PHONY: all clean

all: $(PROG)

$(PROG): cpptools-proxy.c
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) -o $(PROG) cpptools-proxy.c

clean:
	rm -f $(PROG)

.PHONY: clean
