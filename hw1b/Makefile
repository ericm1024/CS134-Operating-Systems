TARBALL=lab1b-40160869.tar.gz
TARGETS=lab1b-client lab1b-server
CC=clang
CFLAGS=-std=c99 -Wall -Wextra -pedantic
LDFLAGS=-lpthread -lmcrypt

all: $(TARGETS)

lab1b-client: lab1b-client.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

lab1b-server: lab1b-server.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGETS) $(TARBALL)

dist: $(TARBALL)
$(TARBALL): README Makefile $(TARGETS)
	tar -czvf $@ $^
