
counter_test: counter_test.c counter.h
	gcc -Wall -Wextra -std=c99 -O2 -o $@ $< -lpthread

# this test will eat your computer, run with caution
# note that we run the atomic one with 100x more iterations because if we
# don't, the threads don't overlap enough for it to be a propper test.
# (on my machine it's still twice as fast)
test: counter_test
	time ./counter_test m 1000000 64
	time ./counter_test a 100000000 64

clean:
	rm -f counter_test
