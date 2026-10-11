CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

standstill: main.c audio.c audio.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ main.c audio.c $(SDL_LIBS) -lm

run: standstill
	./standstill

clean:
	rm -f standstill

.PHONY: run clean
