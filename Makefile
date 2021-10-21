CFLAGS = -std=c++20 -g -fopenmp
LDFLAGS = -I./include -L./usr/local/include/SDL2 -lpthread -lsfml-graphics -lsfml-window -lsfml-system -lX11 -lgomp

.PHONY: test clean install

test: swatch
	./swatch ./test2.glsl ./test.glsl

clean:
	rm -f swatch

swatch: src/main.cpp
	clang++ $(CFLAGS) -o swatch ./src/main.cpp $(LDFLAGS)
	chmod +x ./swatch

install:
	cp ./swatch ~/.local/bin/swatch