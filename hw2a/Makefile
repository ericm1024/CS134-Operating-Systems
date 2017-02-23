TARGETS=lab2_add lab2_list
CC=gcc
CCFLAGS=-Wall -Wextra -std=gnu99
LDFLAGS=-pthread

all: $(TARGETS)

lab2_add: lab2_add.c
	$(CC) $(CCFLAGS) -o $@ $< $(LDFLAGS)

lab2_list: lab2_list.c SortedList.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)

SortedList.o: SortedList.c
	$(CC) $(CCFLAGS) -c $<

clean:
	rm -rf $(TARGETS) *.o
