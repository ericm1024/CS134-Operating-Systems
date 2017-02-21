# Eric Mueller -- emueller@hmc.edu -- 40160869

TARBALL=lab1b-40160869.tar.gz
TARGETS=lab1b-client lab1b-server
CC=clang
CFLAGS=-std=c99 -Wall -Wextra -pedantic
LDFLAGS=-lpthread -lmcrypt

all: $(TARGETS)

lab1b-client: lab1b-client.c common.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

lab1b-server: lab1b-server.c common.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

common.o: common.c common.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(TARGETS) $(TARBALL)

dist: $(TARBALL)
$(TARBALL): README Makefile *.c *.h my.key
	tar -czvf $@ $^
