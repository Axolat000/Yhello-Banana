APP         = sdl2_demo
TITLE       = SDL2 Demo
VERSION     = 1.0
TYPE        = gui
DESCRIPTION = SDL2 demonstration and qualification tests for Banana OS
AUTHOR      = Banana OS
CATEGORY    = Games
SRCS        = sdl2_demo.c

HERE := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
SDK  := $(realpath $(HERE)/../../../sdk)

CFLAGS += -I$(HERE)/../include
LIBS_x86_64 = $(HERE)/../x86_64/libSDL2.a
LIBS_i686   = $(HERE)/../i686/libSDL2.a

include $(SDK)/banana.mk
