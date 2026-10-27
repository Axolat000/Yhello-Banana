# vi/ex package

This package uses [Neatvi](https://github.com/aligrudi/neatvi), an independent
UTF-8 vi/ex editor under the ISC terms, rather than Vim's tiny build. Vim is
still built separately by ports/vim and owns vim, vimdiff and view. This package
owns vi and ex.

Source is pinned to commit 26e9cadbf3828807a35bbe71f19e323e97b27439 with a
verified archive SHA-256. The unmodified upstream notice is shipped as
/apps/vi/LICENSE and the upstream manual as /apps/vi/README. Configuration is
EXINIT or ~/.neatvi, not ~/.vimrc. This is a vi-style editor, not a claim of
complete POSIX vi compatibility or Vim plugin support.

Build with ports/vi/build.sh OUTPUT.bpk. It needs the application SDK compiler
and has no ncurses or Vim build dependency. Native OS regressions are part of
shelltest -e.
