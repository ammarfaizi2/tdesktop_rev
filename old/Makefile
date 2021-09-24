# SPDX-License-Identifier: GPL-2.0

CC=gcc
CXX=g++
CFLAGS=-ggdb3 -fpic -fPIC -O3 -shared
LDLINK_FLAGS=-lpthread -ldl

all: hack.so inject.so

hack.so: hack.S
	$(CC) $(CFLAGS) $(^) -o $(@)

inject.so: inject.c
	$(CC) $(CFLAGS) $(^) -o $(@) $(LDLINK_FLAGS)

clean:
	rm -vf *.so

.PHONY: all clean
