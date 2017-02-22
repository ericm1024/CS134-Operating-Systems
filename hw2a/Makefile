TARGETS=lab2_add
CC=gcc
CCFLAGS=-Wall -Wextra -std=gnu99 # gnu99 for atomics
LDFLAGS=-pthread

all: $(TARGETS)

lab2_add: lab2_add.c
	$(CC) $(CCFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf $(TARGETS)
