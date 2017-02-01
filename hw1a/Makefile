TARBALL=lab1a-40160869.tar.gz

lab1a: lab1a.c
	gcc -std=c99 -Wall -Wextra -pedantic -o lab1a lab1a.c -lpthread

clean:
	rm -f lab1a $(TARBALL)

dist: $(TARBALL)
$(TARBALL): README Makefile lab1a.c
	tar -czvf $@ $^
