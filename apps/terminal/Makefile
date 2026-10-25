# SPDX-License-Identifier: GPL-3.0-or-later
APP         = terminal
TITLE       = Terminal
VERSION     = 1.2
TYPE        = gui
DESCRIPTION = Integrated Bash terminal with your selected shell in windows and tabs: colours, Unicode, scrollback, copy and paste, the mouse
AUTHOR      = Banana OS
CATEGORY    = System
SRCS        = main.c vt.c prefs.c
ICON        = icon.png

include ../../sdk/banana.mk

$(BUILD)/x86_64/main.o $(BUILD)/x86_64/prefs.o: prefs.h
