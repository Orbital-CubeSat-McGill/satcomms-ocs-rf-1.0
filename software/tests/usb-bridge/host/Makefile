CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -I../src

.PHONY: test clean

test: test_cobs
	./test_cobs

test_cobs: test_cobs.c ../src/cobs.c ../src/cobs.h
	$(CC) $(CFLAGS) -o $@ test_cobs.c ../src/cobs.c

clean:
	rm -f test_cobs
