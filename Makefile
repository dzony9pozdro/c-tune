CFLAGS = -Wall -Wextra -Wshadow -Wconversion -std=gnu23 -g \
         $(shell pkg-config --cflags sdl3)

LDLIBS = $(shell pkg-config --libs sdl3)

main: main.c
	$(CC) $(CFLAGS) main.c $(LDLIBS) -o main

run: main
	./main

clean:
	rm -f main

.PHONY: run clean
