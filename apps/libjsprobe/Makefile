# SPDX-License-Identifier: GPL-3.0-or-later
# Optional internal test worker; never embedded in the normal ISO.
.DEFAULT_GOAL := prototype
APP = libjsprobe
TITLE = LibJS prototype
TYPE = console
VERSION = 0.1
AUTHOR = Banana OS
DESCRIPTION = Experimental isolated Ladybird JavaScript engine probe
SRCS = main.c
DATA = ../../ports/libjs/build/banana/THIRD_PARTY_NOTICES.txt
CFLAGS = -DBANANA_MALLOC_REGION=33554432ul
LIBS_x86_64 = --start-group ../../ports/libjs/build/banana/libjs-prototype.a \
    $(shell g++ -print-file-name=libstdc++.a) $(shell gcc -print-libgcc-file-name) \
    $(shell gcc -print-file-name=libgcc_eh.a) --end-group
LDFLAGS = -Map build/libjs-link.map
include ../../sdk/banana.mk

$(BUILD)/x86_64/main.o: ../../ports/libjs/probe.h
$(BUILD)/app-x86_64: ../../ports/libjs/build/banana/libjs-prototype.a

# SDK defaults contain placeholder type_info vtables for no-RTTI applications.
# This program uses genuine GCC RTTI, without changing other applications.
$(BUILD)/x86_64/sdk_cxxrt.o: $(BANANA_SDK)/lib/cxxrt.c $(SDK_HEADERS) ../../ports/libjs/localize_sdk_runtime.py
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_x86_64) $(APP_CFLAGS) $(NOBUILTIN) -c $< -o $@
	python3 ../../ports/libjs/localize_sdk_runtime.py $@

.PHONY: prototype
prototype: libjsprobe-x86_64.bpk.z
