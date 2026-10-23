
PREFIX = /usr/local
CC = cc

SDL_CFLAGS = $(shell pkg-config --cflags sdl2 SDL2_ttf)
SDL_LIBS   = $(shell pkg-config --libs sdl2 SDL2_ttf)

CFLAGS = -std=c23 -Wpedantic -Wall -Wextra $(SDL_CFLAGS)
LDFLAGS = $(SDL_LIBS)

