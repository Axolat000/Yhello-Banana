; kernel/exbin.asm - the SDK's example apps (.bpk), built by the
; Makefile and embedded in the kernel; kernel/examples.c copies them to
; ~/Examples on a fresh system. Assembled for both kernels (elf32/elf64).

global ex_hello, ex_hello_end, ex_guess, ex_guess_end, ex_paint, ex_paint_end
global ex_clock, ex_clock_end, ex_tones, ex_tones_end, ex_mandel, ex_mandel_end, ex_threads, ex_threads_end, ex_webview, ex_webview_end, ex_gears, ex_gears_end, ex_cltest, ex_cltest_end
global ex_systest, ex_systest_end

section .rodata
align 16
ex_hello: incbin "sdk/examples/hello/hello.bpk"
ex_hello_end:
align 16
ex_guess: incbin "sdk/examples/guess/guess.bpk"
ex_guess_end:
align 16
ex_paint: incbin "sdk/examples/paint/paint.bpk"
ex_paint_end:
align 16
ex_clock: incbin "sdk/examples/clock/clock.bpk"
ex_clock_end:
align 16
ex_tones: incbin "sdk/examples/tones/tones.bpk"
ex_tones_end:
align 16
ex_mandel: incbin "sdk/examples/mandel/mandel.bpk"
ex_mandel_end:
align 16
ex_threads: incbin "sdk/examples/threads/threads.bpk"
ex_threads_end:
ex_webview: incbin "sdk/examples/webview/webview.bpk"
ex_webview_end:
align 16
ex_gears: incbin "sdk/examples/gears/gears.bpk"
ex_gears_end:
align 16
ex_cltest: incbin "sdk/examples/cltest/cltest.bpk"
ex_cltest_end:
align 16
ex_systest: incbin "sdk/examples/systest/systest.bpk"
ex_systest_end:
