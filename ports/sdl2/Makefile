# Makefile for SDL2 on Banana OS
HERE := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
SDK  := $(realpath $(HERE)/../../sdk)

CC ?= gcc
AR ?= ar

CFLAGS_COMMON = -ffreestanding -fno-stack-protector -fstack-clash-protection -fpie \
                -fvisibility=hidden -nostdlib -nostdinc -fno-asynchronous-unwind-tables \
                -fno-exceptions -I$(HERE)/include -I$(SDK)/include -D__BANANA_OS__

all: x86_64/libSDL2.a i686/libSDL2.a

x86_64/libSDL2.a: $(HERE)/src/sdl2_banana.c $(SDK)/lib/gl3.c
	@mkdir -p x86_64
	$(CC) -m64 -mno-red-zone $(CFLAGS_COMMON) -O2 -c $(HERE)/src/sdl2_banana.c -o x86_64/sdl2_banana.o
	$(CC) -m64 -mno-red-zone $(CFLAGS_COMMON) -O2 -c $(SDK)/lib/gl3.c -o x86_64/gl3.o
	$(AR) rcs $@ x86_64/sdl2_banana.o x86_64/gl3.o

i686/libSDL2.a: $(HERE)/src/sdl2_banana.c
	@mkdir -p i686
	$(CC) -m32 -march=i686 $(CFLAGS_COMMON) -O2 -c $< -o i686/sdl2_banana.o
	$(AR) rcs $@ i686/sdl2_banana.o

clean:
	rm -rf x86_64 i686

.PHONY: all clean
