TARBALL=lab0-40160869.tar.gz

lab0: lab0.c
	gcc -g -std=c99 -Wall -Wextra -pedantic -o lab0 lab0.c

check: lab0
	./test

clean:
	rm -f lab0 $(TARBALL)

dist: $(TARBALL)
$(TARBALL): README Makefile lab0.c test gdb_confirm_null_ptr.png gdb_segfault_backtrace.png
	tar -czvf $@ $^

