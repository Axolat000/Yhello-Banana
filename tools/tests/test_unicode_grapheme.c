#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <utf8proc.h>

/* Forward declare functions under test matching apps/code/editor.c implementation */
static int next_char(const char* s, int len, int x) {
    if (x >= len) return len;
    const utf8proc_uint8_t* p = (const utf8proc_uint8_t*)s + x;
    utf8proc_ssize_t rem = len - x;
    utf8proc_int32_t cp1, cp2;
    utf8proc_ssize_t sz1 = utf8proc_iterate(p, rem, &cp1);
    if (sz1 <= 0) return x + 1;
    int cur = x + (int)sz1;
    utf8proc_int32_t state = 0;
    while (cur < len) {
        utf8proc_ssize_t sz2 = utf8proc_iterate((const utf8proc_uint8_t*)s + cur, len - cur, &cp2);
        if (sz2 <= 0) break;
        if (utf8proc_grapheme_break_stateful(cp1, cp2, &state)) break;
        cp1 = cp2;
        cur += (int)sz2;
    }
    return cur;
}

static int prev_char(const char* s, int x) {
    if (x <= 0) return 0;
    int pos = 0;
    while (pos < x) {
        int next = next_char(s, x, pos);
        if (next >= x) return pos;
        pos = next;
    }
    return pos;
}

static int match_at_norm(const char* s, int len, int x,
                         const utf8proc_uint8_t* norm_w, utf8proc_ssize_t norm_wl,
                         utf8proc_option_t opts, int* out_matched_len) {
    if (x >= len) return 0;
    int j = next_char(s, len, x);
    while (j <= len && j <= x + 4 * (int)norm_wl + 16) {
        utf8proc_uint8_t* norm_sub = NULL;
        utf8proc_ssize_t slen = utf8proc_map((const utf8proc_uint8_t*)s + x, j - x, &norm_sub, opts);
        if (slen == norm_wl && norm_sub && memcmp(norm_sub, norm_w, (size_t)norm_wl) == 0) {
            free(norm_sub);
            if (out_matched_len) *out_matched_len = j - x;
            return 1;
        }
        if (norm_sub) free(norm_sub);
        if (j == len) break;
        j = next_char(s, len, j);
    }
    return 0;
}

/* Simulated deletion helpers matching doc_backspace and doc_delete */
static void simulate_backspace(char* buf, int* cx) {
    int len = (int)strlen(buf);
    if (*cx <= 0) return;
    int p = prev_char(buf, *cx);
    memmove(buf + p, buf + *cx, (size_t)(len - *cx + 1));
    *cx = p;
}

static void simulate_delete(char* buf, int cx) {
    int len = (int)strlen(buf);
    if (cx >= len) return;
    int nx = next_char(buf, len, cx);
    memmove(buf + cx, buf + nx, (size_t)(len - nx + 1));
}

int main(void) {
    printf("[TEST] Unicode Grapheme Navigation and Normalized Search\n");

    /* 1. Corpus: Precomposed vs Combining Accents */
    {
        /* 'é' NFC: U+00E9 (0xC3 0xA9 - 2 bytes) */
        const char* nfc_e = "Caf\xC3\xA9";
        int len_nfc = (int)strlen(nfc_e);
        assert(next_char(nfc_e, len_nfc, 3) == 5);
        assert(prev_char(nfc_e, 5) == 3);

        /* 'e' + combining acute U+0301 (0x65 0xCC 0x81 - 3 bytes) */
        const char* nfd_e = "Caf\x65\xCC\x81";
        int len_nfd = (int)strlen(nfd_e);
        assert(next_char(nfd_e, len_nfd, 3) == 6);
        assert(prev_char(nfd_e, 6) == 3);

        /* Backspace simulation on combining mark deletes the whole grapheme cluster */
        char buf[32];
        strcpy(buf, "Caf\x65\xCC\x81");
        int cx = 6;
        simulate_backspace(buf, &cx);
        assert(cx == 3);
        assert(strcmp(buf, "Caf") == 0);

        /* Delete simulation from start of combining mark deletes the whole grapheme cluster */
        strcpy(buf, "Caf\x65\xCC\x81 latte");
        simulate_delete(buf, 3);
        assert(strcmp(buf, "Caf latte") == 0);
        printf("  [PASS] Precomposed vs combining accent navigation & deletion\n");
    }

    /* 2. Corpus: Emoji with Selector and ZWJ Sequence */
    {
        /* Heart with variation selector: U+2764 U+FE0F (0xE2 0x9D 0xA4 0xEF 0xB8 0x8F - 6 bytes) */
        const char* heart = "\xE2\x9D\xA4\xEF\xB8\x8F";
        int len_heart = (int)strlen(heart);
        assert(next_char(heart, len_heart, 0) == 6);
        assert(prev_char(heart, 6) == 0);

        char buf[64];
        strcpy(buf, "\xE2\x9D\xA4\xEF\xB8\x8F!");
        int cx = 6;
        simulate_backspace(buf, &cx);
        assert(cx == 0);
        assert(strcmp(buf, "!") == 0);

        /* Family ZWJ sequence: Man U+1F468, ZWJ U+200D, Woman U+1F469, ZWJ U+200D, Girl U+1F467 */
        const char* fam = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
        int len_fam = (int)strlen(fam);
        assert(next_char(fam, len_fam, 0) == len_fam);
        assert(prev_char(fam, len_fam) == 0);

        strcpy(buf, fam);
        cx = len_fam;
        simulate_backspace(buf, &cx);
        assert(cx == 0);
        assert(buf[0] == '\0');
        printf("  [PASS] Emoji variation selector & ZWJ sequence clusters\n");
    }

    /* 3. Corpus: Multibyte Characters (CJK / Cyrillic) */
    {
        /* CJK: 中 (3 bytes: E4 B8 AD) 文 (3 bytes: E6 96 87) */
        const char* cjk = "\xE4\xB8\xAD\xE6\x96\x87";
        int len_cjk = (int)strlen(cjk);
        assert(next_char(cjk, len_cjk, 0) == 3);
        assert(next_char(cjk, len_cjk, 3) == 6);
        assert(prev_char(cjk, 6) == 3);
        assert(prev_char(cjk, 3) == 0);

        char buf[32];
        strcpy(buf, cjk);
        int cx = 6;
        simulate_backspace(buf, &cx);
        assert(cx == 3);
        assert(strcmp(buf, "\xE4\xB8\xAD") == 0);
        printf("  [PASS] Multibyte CJK characters\n");
    }

    /* 4. Corpus: Invalid UTF-8 handling */
    {
        /* Corrupted UTF-8 bytes: 'a', 0xFF, 0xFE, 'b' */
        const char inv[] = {'a', (char)0xFF, (char)0xFE, 'b', '\0'};
        int len_inv = 4;
        assert(next_char(inv, len_inv, 0) == 1);
        assert(next_char(inv, len_inv, 1) == 2);
        assert(next_char(inv, len_inv, 2) == 3);
        assert(next_char(inv, len_inv, 3) == 4);

        assert(prev_char(inv, 4) == 3);
        assert(prev_char(inv, 3) == 2);
        assert(prev_char(inv, 2) == 1);
        assert(prev_char(inv, 1) == 0);

        char buf[32];
        memcpy(buf, inv, 5);
        int cx = 3; /* before 'b', after 0xFE */
        simulate_backspace(buf, &cx);
        assert(cx == 2);
        assert(buf[0] == 'a' && (unsigned char)buf[1] == 0xFF && buf[2] == 'b');
        printf("  [PASS] Invalid UTF-8 sequence recovery and progression\n");
    }

    /* 5. Normalized Search & Preservation of Original File Bytes */
    {
        /* Haystack contains decomposed text (NFD): "Caf\x65\xCC\x81 latte" */
        const char* orig_haystack = "Caf\x65\xCC\x81 latte";
        char haystack[64];
        strcpy(haystack, orig_haystack);
        int hlen = (int)strlen(haystack);

        /* Needle is precomposed NFC: "Caf\xC3\xA9" */
        const char* needle = "Caf\xC3\xA9";
        int nlen = (int)strlen(needle);

        utf8proc_option_t opts = UTF8PROC_STABLE | UTF8PROC_COMPOSE;
        utf8proc_uint8_t* norm_w = NULL;
        utf8proc_ssize_t norm_wl = utf8proc_map((const utf8proc_uint8_t*)needle, nlen, &norm_w, opts);
        assert(norm_w && norm_wl > 0);

        int mlen = 0;
        int found = match_at_norm(haystack, hlen, 0, norm_w, norm_wl, opts, &mlen);
        assert(found == 1);
        assert(mlen == 6); /* matched 6 bytes of decomposed "Café" in haystack */

        /* Verify original buffer bytes were NOT altered */
        assert(strcmp(haystack, orig_haystack) == 0);
        assert(memcmp(haystack, "Caf\x65\xCC\x81 latte", 12) == 0);
        free(norm_w);

        /* Case-insensitive normalized search: Haystack has "CAFÉ" (NFC), needle has "caf\x65\xCC\x81" (NFD) */
        const char* hay2 = "LE CAF\xC3\x89 AU LAIT";
        const char* ndl2 = "caf\x65\xCC\x81";
        utf8proc_option_t opts_cf = UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_CASEFOLD;
        utf8proc_uint8_t* norm_w2 = NULL;
        utf8proc_ssize_t norm_wl2 = utf8proc_map((const utf8proc_uint8_t*)ndl2, (int)strlen(ndl2), &norm_w2, opts_cf);
        assert(norm_w2 && norm_wl2 > 0);

        mlen = 0;
        found = match_at_norm(hay2, (int)strlen(hay2), 3, norm_w2, norm_wl2, opts_cf, &mlen);
        assert(found == 1);
        assert(mlen == 5); /* "CAFÉ" in NFC is 3 (CAF) + 2 (É) = 5 bytes */
        free(norm_w2);

        printf("  [PASS] Normalized search across NFC/NFD with byte preservation\n");
    }

    printf("[SUCCESS] All Unicode grapheme and search tests passed!\n");
    return 0;
}
