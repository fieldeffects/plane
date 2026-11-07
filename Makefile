CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2

.PHONY: all test example clean

all: test example

test: tests/test_plane
	cd tests && ./test_plane

example: examples/mask
	cd examples && ./mask

tests/test_plane: tests/test_plane.c plane.h
	$(CC) $(CFLAGS) -o $@ tests/test_plane.c

examples/mask: examples/mask.c plane.h
	$(CC) $(CFLAGS) -o $@ examples/mask.c

clean:
	rm -f tests/test_plane examples/mask tests/test_roundtrip.plane examples/fog_mask.plane
