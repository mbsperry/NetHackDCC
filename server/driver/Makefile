# Makefile for nhdcc-driver (Phase 0 spike). Links libnh.a + liblua built at
# the repo root; see README.md for the full build sequence.

REPO   := ../..
LIBNH  := $(REPO)/src/libnh.a
LIBLUA := $(REPO)/submodules/lua/liblua.a

CC     ?= cc
CFLAGS ?= -g -O0 -Wall

# glyphdecode.c includes hack.h, so it must compile with the exact defines
# libnh.a itself was built with (sys/unix/hints/linux.500's WANT_LIBNH
# block) -- a mismatch would silently misalign struct fields gated by these
# (e.g. ENHANCED_SYMBOLS on glyph_map). main.c stays header-free and does
# NOT get these flags.
GLYPHFLAGS := -I$(REPO)/include -DSHIM_GRAPHICS -DNOTTYGRAPHICS -DNOSHELL -DLIBNH -fpic

BIN := nhdcc-driver

.PHONY: all clean

all: $(BIN)

$(BIN): main.o glyphdecode.o $(LIBNH) $(LIBLUA)
	$(CC) -o $@ main.o glyphdecode.o $(LIBNH) $(LIBLUA) -lm

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ main.c

glyphdecode.o: glyphdecode.c
	$(CC) $(CFLAGS) $(GLYPHFLAGS) -c -o $@ glyphdecode.c

clean:
	rm -f $(BIN) main.o glyphdecode.o
