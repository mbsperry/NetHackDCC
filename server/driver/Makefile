# Makefile for nhdcc-driver (Phase 0 spike). Links libnh.a + liblua built at
# the repo root; see README.md for the full build sequence.

REPO   := ../..
LIBNH  := $(REPO)/src/libnh.a
LIBLUA := $(REPO)/submodules/lua/liblua.a

CC     ?= cc
CFLAGS ?= -g -O0 -Wall

# glyphdecode.c and snapshot.c include hack.h, so they must compile with the
# exact defines libnh.a itself was built with (sys/unix/hints/linux.500's
# WANT_LIBNH block) -- a mismatch would silently misalign struct fields
# gated by these (e.g. ENHANCED_SYMBOLS on glyph_map). main.c stays
# header-free and does NOT get these flags.
ENGINEFLAGS := -I$(REPO)/include -DSHIM_GRAPHICS -DNOTTYGRAPHICS -DNOSHELL -DLIBNH -fpic

BIN := nhdcc-driver
ENGINE_OBJS := glyphdecode.o snapshot.o

.PHONY: all clean

all: $(BIN)

$(BIN): main.o $(ENGINE_OBJS) $(LIBNH) $(LIBLUA)
	$(CC) -o $@ main.o $(ENGINE_OBJS) $(LIBNH) $(LIBLUA) -lm

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ main.c

glyphdecode.o: glyphdecode.c
	$(CC) $(CFLAGS) $(ENGINEFLAGS) -c -o $@ glyphdecode.c

snapshot.o: snapshot.c
	$(CC) $(CFLAGS) $(ENGINEFLAGS) -c -o $@ snapshot.c

clean:
	rm -f $(BIN) main.o $(ENGINE_OBJS)
