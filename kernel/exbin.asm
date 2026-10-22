; SPDX-License-Identifier: GPL-3.0-or-later
; kernel/exbin.asm - the SDK's example apps (.bpk, zlib-compressed: .bpk.z), built by the
; Makefile and embedded in the kernel; kernel/examples.c copies them to
; ~/Examples on a fresh system. Assembled for both kernels (elf32/elf64).

global ex_hello, ex_hello_end, ex_guess, ex_guess_end, ex_paint, ex_paint_end
global ex_clock, ex_clock_end, ex_tones, ex_tones_end, ex_mandel, ex_mandel_end, ex_threads, ex_threads_end, ex_webview, ex_webview_end, ex_gears, ex_gears_end, ex_cltest, ex_cltest_end
global ex_systest, ex_systest_end, ex_posixtest, ex_posixtest_end, ex_mousetest, ex_mousetest_end, ex_shelltest, ex_shelltest_end

section .payload progbits alloc noexec nowrite align=16
align 16
ex_hello: incbin "sdk/examples/hello/hello.bpk.z"
ex_hello_end:
align 16
ex_guess: incbin "sdk/examples/guess/guess.bpk.z"
ex_guess_end:
align 16
ex_paint: incbin "sdk/examples/paint/paint.bpk.z"
ex_paint_end:
align 16
ex_clock: incbin "sdk/examples/clock/clock.bpk.z"
ex_clock_end:
align 16
ex_tones: incbin "sdk/examples/tones/tones.bpk.z"
ex_tones_end:
align 16
ex_mandel: incbin "sdk/examples/mandel/mandel.bpk.z"
ex_mandel_end:
align 16
ex_threads: incbin "sdk/examples/threads/threads.bpk.z"
ex_threads_end:
ex_webview: incbin "sdk/examples/webview/webview.bpk.z"
ex_webview_end:
align 16
ex_gears: incbin "sdk/examples/gears/gears.bpk.z"
ex_gears_end:
align 16
ex_cltest: incbin "sdk/examples/cltest/cltest.bpk.z"
ex_cltest_end:
align 16
ex_systest: incbin "sdk/examples/systest/systest.bpk.z"
ex_systest_end:
align 16
ex_posixtest: incbin "sdk/examples/posixtest/posixtest.bpk.z"
ex_posixtest_end:

align 16
ex_mousetest: incbin "sdk/examples/mousetest/mousetest.bpk.z"
ex_mousetest_end:
ex_shelltest: incbin "sdk/examples/shelltest/shelltest.bpk.z"
ex_shelltest_end:

align 16
global ex_gl3test, ex_gl3test_end
ex_gl3test: incbin "sdk/examples/gl3test/gl3test.bpk.z"
ex_gl3test_end:
