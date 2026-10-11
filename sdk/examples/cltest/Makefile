APP         = cltest
TITLE       = OpenCL test
VERSION     = 1.0
TYPE        = console
CATEGORY    = Developer Tools
DESCRIPTION = OpenCL kernels on every core: vector add, Mandelbrot, atomics, math - checked against the processor
AUTHOR      = Banana OS SDK
SRCS        = cltest.c
USE_CL      = 1
LIBS_i686   = $(shell gcc -m32 -print-libgcc-file-name)
LIBS_x86_64 = $(shell gcc -m64 -print-libgcc-file-name)

include ../../banana.mk
