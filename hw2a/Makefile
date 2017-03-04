# NAME: Eric Mueller
# EMAIL: emueller@hmc.edu
# ID: 40160869

TARBALL=lab2a-40160869.tar.gz
TARGETS=lab2_add lab2_list
CC=gcc
CCFLAGS=-Wall -Wextra -std=gnu99
LDFLAGS=-pthread

build: $(TARGETS)

lab2_add: lab2_add.c
	$(CC) $(CCFLAGS) -o $@ $< $(LDFLAGS)

lab2_list: lab2_list.c SortedList.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)

SortedList.o: SortedList.c
	$(CC) $(CCFLAGS) -c $<

tests: lab2_add.csv lab2_list.csv

lab2_add.csv: lab2_add
	./lab2_add_generate.sh

lab2_list.csv: lab2_list
	./lab2_list_generate.sh

graphs: tests
	./lab2_add.gp
	./lab2_list.gp

clean:
	rm -rf $(TARGETS) *.o *.csv *.png

dist: $(TARBALL) graphs
$(TARBALL): README Makefile *.c *.h *.sh *.gp *.csv *.png
	tar -czvf $@ $^
