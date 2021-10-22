CC = clang++
FLAGS = -std=c++20 -g -fopenmp
LDFLAGS = -I./src -lpthread -lsfml-graphics -lsfml-window -lsfml-system -lX11 -lgomp
BUILDDIR = ./build

SOURCES = $(wildcard src/*.cpp)
OBJECTS = $(patsubst src/%.cpp,build/%.o,$(SOURCES))

.PHONY: build test clean install

build: $(BUILDDIR)/swatch

# Link all the objects
$(BUILDDIR)/swatch: $(OBJECTS)
	$(CC) $(FLAGS) $^ -o $@ $(LDFLAGS)

# Build each source file (compilation unit)
$(OBJECTS): $(BUILDDIR)/%.o : ./src/%.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $(FLAGS) $< -o $@

# Test with some example shader files
test: build
	$(BUILDDIR)/swatch ./test2.glsl ./test.glsl

# Installation and cleaning
clean:
	rm -rf $(BUILDDIR)

install: build
	cp $(BUILDDIR)/swatch ~/.local/bin/swatch