CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic

.PHONY: all test

all: build/sun

build/sun: src/sun.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@

test: build/sun
	python3 -m unittest discover -s tests -v
