/* Standalone Unicode demonstration using utf8proc.
 * Tests normalization (NFC/NFD), grapheme clustering, case folding,
 * and character categories without depending on shell/editor.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utf8proc.h>

static int run_tests(void) {
    int failed = 0;

    printf("=== utf8proc Version: %s ===\n", utf8proc_version());

    /* 1. Codepoint iteration and decoding */
    const char *text = "Café! 🌍";
    const utf8proc_uint8_t *p = (const utf8proc_uint8_t *)text;
    utf8proc_ssize_t len = (utf8proc_ssize_t)strlen(text);
    utf8proc_int32_t cp = 0;
    utf8proc_ssize_t sz = 0;
    int cp_count = 0;

    while (len > 0) {
        sz = utf8proc_iterate(p, len, &cp);
        if (sz <= 0) {
            printf("[FAIL] utf8proc_iterate returned %ld\n", (long)sz);
            failed++;
            break;
        }
        p += sz;
        len -= sz;
        cp_count++;
    }
    if (cp_count == 7) { /* C, a, f, é, !, ' ', 🌍 */
        printf("[OK] Codepoint iteration count: %d\n", cp_count);
    } else {
        printf("[FAIL] Expected 7 codepoints, got %d\n", cp_count);
        failed++;
    }

    /* 2. Unicode normalization (NFC and NFD) */
    /* "é" precomposed: U+00E9 (0xC3 0xA9) */
    /* "e + combining acute": U+0065 U+0301 (0x65 0xCC 0x81) */
    const char *nfc_input = "\xC3\xA9";
    const char *nfd_input = "\x65\xCC\x81";

    utf8proc_uint8_t *nfc_norm = utf8proc_NFC((const utf8proc_uint8_t *)nfd_input);
    if (nfc_norm && strcmp((const char *)nfc_norm, nfc_input) == 0) {
        printf("[OK] Normalization NFD -> NFC successful\n");
    } else {
        printf("[FAIL] Normalization NFD -> NFC failed\n");
        failed++;
    }
    if (nfc_norm) free(nfc_norm);

    utf8proc_uint8_t *nfd_norm = utf8proc_NFD((const utf8proc_uint8_t *)nfc_input);
    if (nfd_norm && strcmp((const char *)nfd_norm, nfd_input) == 0) {
        printf("[OK] Normalization NFC -> NFD successful\n");
    } else {
        printf("[FAIL] Normalization NFC -> NFD failed\n");
        failed++;
    }
    if (nfd_norm) free(nfd_norm);

    /* 3. Grapheme break testing */
    /* In "e\xCC\x81", 'e' and combining acute belong to the same grapheme cluster */
    utf8proc_int32_t state = 0;
    utf8proc_bool brk1 = utf8proc_grapheme_break_stateful(0x0065, 0x0301, &state);
    if (!brk1) {
        printf("[OK] Grapheme boundary: combining acute does NOT break cluster\n");
    } else {
        printf("[FAIL] Grapheme boundary: combining acute incorrectly broke cluster\n");
        failed++;
    }

    /* 4. Case folding */
    utf8proc_int32_t lower_sigma = utf8proc_tolower(0x03A3); /* Greek capital Sigma */
    if (lower_sigma == 0x03C3) {
        printf("[OK] Case folding tolower(U+03A3) == U+03C3\n");
    } else {
        printf("[FAIL] tolower failed, got 0x%04X\n", lower_sigma);
        failed++;
    }

    /* 5. Character category */
    utf8proc_category_t cat = utf8proc_category(0x0041); /* 'A' -> Lu */
    if (cat == UTF8PROC_CATEGORY_LU) {
        printf("[OK] Category of 'A' is Lu (Uppercase Letter)\n");
    } else {
        printf("[FAIL] Category check failed, got %d\n", cat);
        failed++;
    }

    return failed;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("Starting Unicode Demo (utf8proc)...\n");
    int failed = run_tests();
    if (failed == 0) {
        printf("All Unicode tests passed successfully!\n");
        return 0;
    } else {
        printf("%d Unicode test(s) failed.\n", failed);
        return 1;
    }
}
