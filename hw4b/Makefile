# NAME: Eric Mueller
# EMAIL: emueller@hmc.edu
# ID: 40160869

TARBALL=lab4b-40160869.tar.gz
TARGETS=lab4b
CC=gcc
CFLAGS=-Wall -Wextra -std=gnu99 -g
LDFLAGS=-pthread -lmraa -lm -lreadline

build: $(TARGETS)

lab4b: lab4b.c
	$(CC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(TARGETS)

dist: $(TARBALL)
$(TARBALL): lab4b.c
	tar -czvf $@ lab4b.c
