CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic

.PHONY: all test

all: build/sun

SUN_SOURCES := src/main.cpp src/lexer.cpp src/parser.cpp src/codegen.cpp src/modules.cpp src/stdlib_io.cpp

build/sun: $(SUN_SOURCES) src/compiler.hpp src/lexer.hpp src/stdlib_io.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SUN_SOURCES) -o $@

test: build/sun
	python3 -m unittest discover -s tests -v
