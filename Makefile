CFLAGS = -std=c++20
LDFLAGS = -I./include -L./usr/local/include/SDL2 -lpthread -lsfml-graphics -lsfml-window -lsfml-system

.PHONY: test clean

test: swatch
	./swatch ./test.glsl ./test.jpeg

clean:
	rm -f ssdisp

swatch: src/main.cpp
	clang++ $(CFLAGS) -o swatch ./src/main.cpp $(LDFLAGS)