lab0: lab0.c
	gcc -g -std=c99 -Wall -Wextra -pedantic -o lab0 lab0.c

check: lab0
	./test

clean:
	rm -f lab0 lab0-emueller.tar.gz

dist: lab0-emueller.tar.gz
lab0-emueller.tar.gz: README Makefile lab0.c test
	tar -czvf $@ $^

