# NAME: Eric Mueller
# EMAIL: emueller@hmc.edu
# ID: 40160869

TARBALL=lab3a-40160869.tar.gz
TARGETS=lab3a
CC=gcc
CFLAGS=-Wall -pedantic -Wextra -Wformat -std=c99 -g

build: $(TARGETS)

lab3a: lab3a.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(TARGETS)

dist: $(TARBALL)
$(TARBALL): lab3a.c README Makefile ext2_fs.h
	tar -czvf $@ lab3a.c README Makefile ext2_fs.h
