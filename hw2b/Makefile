# NAME: Eric Mueller
# EMAIL: emueller@hmc.edu
# ID: 40160869

TARBALL=lab2b-40160869.tar.gz
TARGETS=lab2_list
CC=gcc
CCFLAGS=-Wall -Wextra -std=gnu99 -g
LDFLAGS=-pthread

build: $(TARGETS)

lab2_list: lab2_list.c SortedList.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)

SortedList.o: SortedList.c
	$(CC) $(CCFLAGS) -c $<

tests: build
	./lab2_list_generate.sh

graphs: tests
	./lab2_list.gp

profile: lab2_list
	perf record -o lab2_list.perf ./lab2_list --iterations=1000 --sync=s \
		--threads=12
	perf report -i lab2_list.perf > lab2_list_perf_report.txt

clean:
	rm -rf $(TARGETS) *.o *.csv *.png *.txt *.perf

dist: $(TARBALL)
$(TARBALL): build profile graphs 
	tar -czvf $@ lab2_list.c SortedList.c SortedList.h \
		lab2_list_perf_report.txt README Makefile lab2b_1.png \
		lab2b_2.png lab2b_3.png lab2b_4.png lab2b_5.png lab2b_list.csv
