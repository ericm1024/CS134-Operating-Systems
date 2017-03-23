# NAME: Eric Mueller
# EMAIL: emueller@hmc.edu
# ID: 40160869

TARBALL=lab4b-40160869.tar.gz
TARGETS=lab4b
CC=gcc
CFLAGS=-Wall -pedantic -Wextra -Wformat -std=c99 -g
LDFLAGS=-pthread -lmraa -lm

build: $(TARGETS)

lab4b: lab4b.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(TARGETS)

check: $(TARGETS)
	@echo "make check currently does nothing"

dist: $(TARBALL)
$(TARBALL): lab4b.c README Makefile
	tar -czvf $@ lab4b.c README Makefile
