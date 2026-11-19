CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2

.PHONY: test clean

test: build/test
	./build/test

build/test: tests/test.cpp src/clob.cpp include/clob.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -o $@ tests/test.cpp src/clob.cpp

clean:
	rm -rf build
