#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# vi for Banana OS: Vim's tiny build (/bin/vi, /bin/ex), an optional package
# (`apt install vi`) - built from ports/vim's source by its build.sh.
#     ports/vi/build.sh <output.bpk>
VIM_TINY=1 exec "$(dirname "$0")/../vim/build.sh" "$@"
