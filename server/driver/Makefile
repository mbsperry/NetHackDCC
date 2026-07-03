# Makefile for nhdcc-driver (Phase 0 spike). Links libnh.a + liblua built at
# the repo root; see README.md for the full build sequence.

REPO   := ../..
LIBNH  := $(REPO)/src/libnh.a
LIBLUA := $(REPO)/submodules/lua/liblua.a

CC     ?= cc
CFLAGS ?= -g -O0 -Wall

BIN := nhdcc-driver

.PHONY: all clean

all: $(BIN)

$(BIN): main.o $(LIBNH) $(LIBLUA)
	$(CC) -o $@ main.o $(LIBNH) $(LIBLUA) -lm

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ main.c

clean:
	rm -f $(BIN) main.o
