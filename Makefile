# NoteCraft - a Minecraft-style Book and Quill for Linux (C11 + SDL2, no Electron)
#
#   make            build ./notecraft
#   make test       run the unit tests (no SDL needed)
#   make demo       build the headless renderer used for screenshots / demo GIF
#   sudo make install [PREFIX=/usr/local]

CC       ?= cc
CFLAGS   ?= -O2
override CFLAGS += -std=c11 -Wall -Wextra -Wno-unused-parameter -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
PREFIX   ?= /usr/local

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null)

CORE_SRC := src/utf.c src/gfx.c src/font.c src/layout.c src/book.c src/field.c src/txt.c \
            src/zipio.c src/epub.c src/png.c src/theme.c src/app.c
CORE_OBJ := $(CORE_SRC:src/%.c=build/%.o)
LIBS     := -lz

ifneq ($(wildcard src/icon_data.h),)
override CFLAGS += -DNC_HAVE_ICON
endif

all: notecraft

build/%.o: src/%.c $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

notecraft: $(CORE_OBJ) build/platform_sdl.o
	$(CC) $(CFLAGS) $^ -o $@ $(SDL_LIBS) $(LIBS) -lm

notecraft-demo: $(CORE_OBJ) build/demo.o
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS) -lm

demo: notecraft-demo

notecraft-test: $(CORE_OBJ) build/test_core.o
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS) -lm

build/test_core.o: tests/test_core.c $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

test: notecraft-test
	./notecraft-test

install: notecraft
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/applications \
	         $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps
	install -m755 notecraft $(DESTDIR)$(PREFIX)/bin/notecraft
	install -m644 packaging/notecraft.desktop $(DESTDIR)$(PREFIX)/share/applications/notecraft.desktop
	install -m644 assets/notecraft.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/notecraft.svg
	install -m644 assets/icon-256.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/notecraft.png

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/notecraft $(DESTDIR)$(PREFIX)/share/applications/notecraft.desktop \
	      $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/notecraft.svg \
	      $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/notecraft.png

clean:
	rm -rf build notecraft notecraft-demo notecraft-test

.PHONY: all demo test install uninstall clean
