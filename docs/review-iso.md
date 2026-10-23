# Review ISOs

The Build review ISO workflow builds the checked-out PR merge revision using
the same native Makefile, application/port dependency installation and cache
layout as the ordinary ISO build. It can also be dispatched for a branch.
Download its Banana-OS-review artifact from the workflow run; it contains
Banana_OS.iso, the checked-out commit and the ISO's SHA-256.

This workflow needs only contents:read. It does not load Secure Boot signing
keys or publish releases. PR code runs through pull_request, never
pull_request_target. Review images are unsigned and retained for seven days;
they are testing artifacts, not releases or hardware qualification. GitHub's
normal approval requirements for external contributions still apply.
