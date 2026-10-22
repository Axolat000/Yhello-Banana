; SPDX-License-Identifier: GPL-3.0-or-later
; kernel/fontbin.asm - the system fonts (DejaVu, fonts/LICENSE), embedded in
; the kernel; kernel/font.c reads them in place. Assembled for both kernels.

global font_sans, font_sans_end, font_sans_bold, font_sans_bold_end
global font_mono, font_mono_end, font_mono_bold, font_mono_bold_end
global font_emoji, font_emoji_end, font_cjk, font_cjk_end, font_hangul, font_hangul_end

section .rodata
align 16
font_sans: incbin "fonts/DejaVuSans.ttf"
font_sans_end:
align 16
font_sans_bold: incbin "fonts/DejaVuSans-Bold.ttf"
font_sans_bold_end:
align 16
font_mono: incbin "fonts/DejaVuSansMono.ttf"
font_mono_end:
align 16
font_mono_bold: incbin "fonts/DejaVuSansMono-Bold.ttf"
font_mono_bold_end:
align 16
font_emoji: incbin "fonts/NotoEmoji-Regular.ttf"       ; (Noto Emoji, SIL OFL 1.1: fonts/LICENSE-NotoEmoji)
font_emoji_end:
align 16
font_cjk: incbin "fonts/DroidSansFallbackFull.ttf"    ; (Droid Sans Fallback, Apache 2.0: fonts/LICENSE-DroidSansFallback)
font_cjk_end:
align 16
font_hangul: incbin "fonts/NanumGothic-Regular.ttf"   ; (Nanum Gothic, SIL OFL 1.1: fonts/LICENSE-NanumGothic)
font_hangul_end:
