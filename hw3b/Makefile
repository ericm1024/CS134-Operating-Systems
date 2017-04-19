# NAME: Eric Mueller
# EMAIL: emueller@hmc.edu
# ID: 40160869

TARBALL=lab3b-40160869.tar.gz
TARGETS=lab3b
CXX=clang++
CXXFLAGS=-Wall -pedantic -Wextra -Wformat -std=c++11 -g

build: $(TARGETS)

lab3b: lab3b.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(TARGETS)

dist: $(TARBALL)
$(TARBALL): lab3b.cpp README Makefile
	tar -czvf $@ lab3b.cpp README Makefile
