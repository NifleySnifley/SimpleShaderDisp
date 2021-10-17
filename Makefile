CFLAGS = -std=c++20 -g
LDFLAGS = -I./include -L./usr/local/include/SDL2 -lpthread -lsfml-graphics -lsfml-window -lsfml-system

.PHONY: test clean

test: swatch
	./swatch ./test2.glsl ./test.glsl ./test.jpeg

clean:
	rm -f swatch

swatch: src/main.cpp
	clang++ $(CFLAGS) -o swatch ./src/main.cpp $(LDFLAGS)