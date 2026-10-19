/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Uses the production parser (including its private parse_len) and real
 * DOM/stylesheet/style fields, with the existing host kernel shims.
 * cc -std=c99 -Wall -Wextra -Iweb/test/shim -Iweb \
 *    web/arena.c web/html.c web/test/css_math_test.c \
 *    -o /tmp/css_math_test && /tmp/css_math_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../css.c"

static int math_checks, math_failures;

static void math_check(int ok, const char* name) {
    math_checks++;
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); math_failures++; }
}

static void length_case(const char* text, int font, int base, int expected, int expected_pct) {
    int pct = 12345;
    int actual = parse_len(text, font, base, &pct);
    math_checks++;
    if (actual != expected || pct != expected_pct) {
        fprintf(stderr, "FAIL: %s (font=%d base=%d): got %d/%d%%, want %d/%d%%\n",
                text ? text : "(null)", font, base, actual, pct, expected, expected_pct);
        math_failures++;
    }
}

static dom_node_t* math_fixture(arena_t* a, const char* css) {
    const char* html = "<html><body><div id='target' class='space-y-4 w-1/2'></div></body></html>";
    dom_node_t* doc = html_parse(a, html, (uint32_t)strlen(html));
    css_sheet_t* sheet = css_parse(a, css, (uint32_t)strlen(css), 1);
    css_style_tree(a, doc, &sheet, 1);
    return dom_find_id(doc, "target");
}

static void math_styles(void) {
    arena_t a;
    arena_init(&a, 0);
    dom_node_t* el = math_fixture(&a,
        "#target{font-size:20px;width:calc(1rem * (1 - .25));"
        "height:calc(.5 * 3rem);margin-top:calc(1rem * (1 - .25));"
        "margin-bottom:calc(1rem * .25);margin-left:calc(-.5 * 1rem);"
        "padding-top:calc(.125rem * 4);padding-right:min(10px, 2px);"
        "padding-bottom:max(2px, .5rem);padding-left:clamp(2px, 99px, 1rem);"
        "left:calc(-(1rem + 4px));top:calc(1em / 2.5);"
        "right:calc(2px * 3px);bottom:calc(2px / 0);"
        "row-gap:calc(.5rem * 3);column-gap:clamp(2px, .25rem, 8px);"
        "max-width:max(20px, min(80px, 60px));flex-basis:calc((2 + .5) * 1rem)}");
    style_t* st = el->style;
    math_check(st->width == 12 && st->height == 24, "width and height fractions");
    math_check(st->margin[0] == 12 && st->margin[2] == 4, "Tailwind spacing inverse after substitution");
    math_check(st->margin[3] == -8, "negative margin scalar first");
    math_check(st->padding[0] == 8 && st->padding[1] == 2 && st->padding[2] == 8 && st->padding[3] == 16,
               "padding min/max/clamp and fractional rem");
    math_check(st->left == -20 && st->top == 8, "unary group and font-relative division");
    math_check(st->right == LEN_AUTO && st->bottom == LEN_AUTO, "invalid offset dimensions and division");
    math_check(st->gap_row == 24 && st->gap_col == 4, "gap longhands");
    math_check(st->max_width == 60 && st->flex_basis == 40, "nested bounds and flex basis");
    math_check(!a.oom, "style fixture arena");
    arena_free_all(&a);

    arena_init(&a, 0);
    el = math_fixture(&a, ".w-1\\/2{width:50%}#target{height:calc(50% - 1rem);max-width:min(50%, 20px)}");
    math_check(el->style->width == LEN_AUTO && el->style->width_pct == 50, "simple width percentage ABI");
    math_check(el->style->height == LEN_AUTO && el->style->max_width == LEN_AUTO,
               "unknown percentages never use viewport for mixed math");
    arena_free_all(&a);

    arena_init(&a, 0);
    el = math_fixture(&a,
        ".space-y-4{--tw-space-y-reverse:.25;"
        "margin-top:calc(1rem * (1 - var(--tw-space-y-reverse)));"
        "margin-bottom:calc(1rem * var(--tw-space-y-reverse))}");
    math_check(el->style->margin[0] == 12 && el->style->margin[2] == 4,
               "Tailwind real custom-property substitution and inverse");
    arena_free_all(&a);
}

static style_t* math_declarations(arena_t* a, const char* declarations) {
    char css[4096];
    snprintf(css, sizeof(css), "#target{%s}", declarations);
    arena_init(a, 0);
    return math_fixture(a, css)->style;
}

static void math_shorthands(void) {
    arena_t a;
    style_t* st;
    static const struct { const char* css; int sides[4]; } padding[] = {
        { "padding:1.5rem", {24,24,24,24} },
        { "padding:1rem .5rem", {16,8,16,8} },
        { "padding:1px 2px 3px", {1,2,3,2} },
        { "padding:1px 2px 3px 4px", {1,2,3,4} },
        { "padding:calc(1rem / 2) min(3px, 5px) max(4px, 6px) clamp(1px, 9px, 4px)", {8,3,6,4} },
        { "padding:9px;padding:1px 2px 3px 4px 5px", {9,9,9,9} },
        { "padding:9px;padding:calc(1px * 2px) 2px", {9,9,9,9} },
        { "padding:9px;padding:1px garbage", {9,9,9,9} }
    };
    for (unsigned i = 0; i < sizeof(padding) / sizeof(padding[0]); i++) {
        st = math_declarations(&a, padding[i].css);
        math_check(memcmp(st->padding, padding[i].sides, sizeof(st->padding)) == 0, padding[i].css);
        arena_free_all(&a);
    }
    st = math_declarations(&a, "margin:calc(1rem * (1 - .25)) auto;inset:1px calc(1rem / 2) 3px 4px");
    math_check(st->margin[0] == 12 && st->margin[2] == 12 && st->margin[1] == 0 && st->margin_auto_lr,
               "margin math and auto tokens");
    math_check(st->top == 1 && st->right == 8 && st->bottom == 3 && st->left == 4, "inset four tokens");
    arena_free_all(&a);

    st = math_declarations(&a, "border:0 solid #e5e7eb;border-top:calc(1px + min(1px, 2px)) solid rgb(1 2 3)");
    math_check(st->border[0] == 2 && st->border[1] == 0 && st->border[2] == 0 && st->border[3] == 0,
               "Tailwind zero border and nested border width");
    math_check(st->border_color[0] == 0x010203 && st->border_color[1] == 0xe5e7eb, "border color function token");
    arena_free_all(&a);
    st = math_declarations(&a, "border:2px solid red;border:calc(2px * 2px) solid blue");
    math_check(st->border[0] == 2 && st->border_color[0] == 0xff0000, "invalid border math preserves preceding border");
    arena_free_all(&a);
    st = math_declarations(&a, "border-width:1px calc(.125rem * 2) 3px 4px");
    math_check(st->border[0] == 1 && st->border[1] == 4 && st->border[2] == 3 && st->border[3] == 4,
               "border-width four components");
    arena_free_all(&a);
    static const struct { const char* css; int row, col; } gaps[] = {
        { "gap:1rem", 16,16 }, { "gap:1rem .5rem", 16,8 },
        { "gap:calc(1rem / 2) calc(2rem * (1 - .25))", 8,24 },
        { "grid-gap:min(8px, 1rem) clamp(2px, calc(.5rem + 2px), 12px)", 8,10 },
        { "gap:3px;gap:1px 2px 3px", 3,3 },
        { "gap:3px;gap:calc(2px / 0) 1px", 3,3 }
    };
    for (unsigned i = 0; i < sizeof(gaps) / sizeof(gaps[0]); i++) {
        st = math_declarations(&a, gaps[i].css);
        math_check(st->gap_row == gaps[i].row && st->gap_col == gaps[i].col, gaps[i].css);
        arena_free_all(&a);
    }
    st = math_declarations(&a, "background-size:calc(4rem / 2) max(12px, 1.5rem);background-position:calc(1rem - 6px) min(12px, 1rem)");
    math_check(st->bg_size == BG_SIZE_PX && st->bg_size_w == 32 && st->bg_size_h == 24, "paired math background size");
    math_check(st->bg_pos_x == 10 && st->bg_pos_y == 12 && st->bg_pos_pct == 0, "paired math background position");
    arena_free_all(&a);
    st = math_declarations(&a, "background-size:auto calc(1rem / 2);background-position:calc(50% * .5) 0%");
    math_check(st->bg_size == BG_SIZE_PX && st->bg_size_w == LEN_AUTO && st->bg_size_h == 8, "auto width and math height background size");
    math_check(st->bg_pos_x == 25 && st->bg_pos_y == 0 && st->bg_pos_pct == 3, "pure percentage math and zero percent position");
    arena_free_all(&a);
    st = math_declarations(&a, "background-position:3px 4px;background-position:calc(50% - 1rem) 2px");
    math_check(st->bg_pos_x == 3 && st->bg_pos_y == 4 && st->bg_pos_pct == 0, "background mixed percentage stays unresolved");
    arena_free_all(&a);
    st = math_declarations(&a, "background:url(\"asset / tile.png\") calc(2rem / 2) 25% / calc(3rem / 2) max(8px, 1rem) no-repeat #123456");
    math_check(st->bg_size == BG_SIZE_PX && st->bg_size_w == 24 && st->bg_size_h == 16, "background shorthand size and trailing repeat/color");
    math_check(st->bg_pos_x == 16 && st->bg_pos_y == 25 && st->bg_pos_pct == 2, "background shorthand math position");
    math_check(st->bg_url_len == strlen("asset / tile.png") && !strncmp(st->bg_url, "asset / tile.png", st->bg_url_len), "quoted URL slash stays in token");
    math_check(st->bg_repeat == BG_NO_REPEAT && st->bg == 0x123456, "background trailing repeat and color");
    arena_free_all(&a);
    st = math_declarations(&a, "background:linear-gradient(red, blue) center / cover");
    math_check(st->bg_size == BG_SIZE_COVER && st->bg_pos_x == 50 && st->bg_pos_y == 50, "gradient background size separator without URL");
    arena_free_all(&a);
    static const struct { const char* css; int size, lh, pct, bold, italic, mono; } fonts[] = {
        { "font:14px/1.5 \"Courier New\", serif", 14,0,150,0,0,1 },
        { "font:italic 700 calc(1rem + 2px) / calc(1em + 3px) \"Counter / Face\", monospace", 18,21,0,1,1,0 },
        { "font:700 16px/24px sans-serif", 16,24,0,1,0,0 },
        { "font:calc(32px / 2)/calc(3rem / 2) serif", 16,24,0,0,0,0 },
        { "font:bold 120% / 150% monospace", 19,0,150,1,0,1 },
        { "font:20px \"slash / name\", serif", 20,0,0,0,0,0 },
        { "line-height:calc(1 + .5)", 16,0,150,0,0,0 }
    };
    for (unsigned i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        st = math_declarations(&a, fonts[i].css);
        if (st->font_px != fonts[i].size || st->lh_px != fonts[i].lh || st->lh_pct != fonts[i].pct ||
            st->bold != fonts[i].bold || st->italic != fonts[i].italic || st->mono != fonts[i].mono)
            fprintf(stderr, "font fields: size=%d lh=%d/%d bold=%d italic=%d mono=%d\n",
                    st->font_px, st->lh_px, st->lh_pct, st->bold, st->italic, st->mono);
        math_check(st->font_px == fonts[i].size && st->lh_px == fonts[i].lh && st->lh_pct == fonts[i].pct &&
                   st->bold == fonts[i].bold && st->italic == fonts[i].italic && st->mono == fonts[i].mono, fonts[i].css);
        arena_free_all(&a);
    }
    static const struct { const char* css; int grow, shrink, basis; } flexes[] = {
        { "flex:2 .5 calc(1rem * (1 - .25))", 200,50,12 },
        { "flex:2 auto", 200,100,LEN_AUTO }, { "flex:1 1 0%", 100,100,0 },
        { "flex:calc(1 + 1) 1 min(40px, 2rem)", 200,100,32 },
        { "flex:2 0 12px;flex:1 1 calc(2px * 2px)", 200,0,12 },
        { "flex:none", 0,0,LEN_AUTO }, { "flex:auto", 100,100,LEN_AUTO },
        { "flex:.5", 50,100,0 }, { "flex:2rem", 100,100,32 }
    };
    for (unsigned i = 0; i < sizeof(flexes) / sizeof(flexes[0]); i++) {
        st = math_declarations(&a, flexes[i].css);
        math_check(st->flex_grow == flexes[i].grow && st->flex_shrink == flexes[i].shrink && st->flex_basis == flexes[i].basis, flexes[i].css);
        arena_free_all(&a);
    }
    char long_value[1200];
    strcpy(long_value, "padding:7px;padding:calc(1px");
    memset(long_value + strlen(long_value), ' ', 600);
    strcpy(long_value + strlen("padding:7px;padding:calc(1px") + 600, " + 1px)");
    st = math_declarations(&a, long_value);
    math_check(st->padding[0] == 7 && st->padding[1] == 7, "overlong shorthand component rejected without truncation");
    arena_free_all(&a);
    css_viewport_w = 300;
    css_viewport_h = 900;
    length_case("10vh", 16, -1, 90, 0);
    length_case("10vmin", 16, -1, 30, 0);
    length_case("10vmax", 16, -1, 90, 0);
    st = math_declarations(&a, "width:calc(50vmin - 1rem);height:calc(50vh - 1rem)");
    math_check(st->width == 134 && st->height == 434, "portrait viewport dimensions reach style fields");
    arena_free_all(&a);
    css_viewport_w = 800;
    css_viewport_h = 600;
}

static void math_tailwind_fixture(void) {
    FILE* f = fopen("web/fixtures/tailwind.css", "rb");
    math_check(f != NULL, "open actual generated Tailwind v3 fixture");
    if (!f) return;
    char* css = (char*)malloc(65537);
    if (!css) { fclose(f); math_check(0, "Tailwind fixture allocation"); return; }
    size_t size = fread(css, 1, 65536, f);
    fclose(f);
    css[size] = 0;
    arena_t a;
    arena_init(&a, 0);
    const char* html = "<html><body><div id='p6' class='p-6'></div>"
        "<div id='axes' class='p-6 px-4 py-2'></div><div id='arbitrary' class='p-[7px]'></div>"
        "<div id='gap' class='flex gap-4 w-1/2'></div><div id='text' class='text-2xl font-bold'></div></body></html>";
    dom_node_t* doc = html_parse(&a, html, (uint32_t)strlen(html));
    css_sheet_t* sheets[] = { css_default_sheet(&a), css_parse(&a, css, (uint32_t)size, 1) };
    css_style_tree(&a, doc, sheets, 2);
    style_t* st = dom_find_id(doc, "p6")->style;
    math_check(st->padding[0] == 24 && st->padding[1] == 24 && st->padding[2] == 24 && st->padding[3] == 24, "actual Tailwind p-6");
    math_check(st->border[0] == 0 && st->border[1] == 0 && st->border[2] == 0 && st->border[3] == 0, "actual Tailwind preflight zero border");
    st = dom_find_id(doc, "axes")->style;
    math_check(st->padding[0] == 8 && st->padding[1] == 16 && st->padding[2] == 8 && st->padding[3] == 16, "actual Tailwind px-4 py-2 over p-6");
    st = dom_find_id(doc, "arbitrary")->style;
    math_check(st->padding[0] == 7 && st->padding[3] == 7, "actual Tailwind arbitrary padding");
    st = dom_find_id(doc, "gap")->style;
    math_check(st->gap_row == 16 && st->gap_col == 16 && st->flex, "actual Tailwind flex gap-4");
    math_check(st->width == LEN_AUTO && st->width_pct == 50, "actual Tailwind width percentage preserved");
    st = dom_find_id(doc, "text")->style;
    if (st->font_px != 24 || st->lh_px != 32 || !st->bold)
        fprintf(stderr, "Tailwind font fields: size=%d lh=%d/%d bold=%d\n", st->font_px, st->lh_px, st->lh_pct, st->bold);
    math_check(st->font_px == 24 && st->lh_px == 32 && st->bold, "actual Tailwind font and line height");
    math_check(!a.oom, "actual Tailwind arena");
    arena_free_all(&a);
    free(css);
}

static void math_overflow_reference(void) {
#ifdef __SIZEOF_INT128__
    /* The host's wider arithmetic is an independent oracle for the portable
     * mul/div path used by both kernels, including products above UINT64_MAX. */
    uint64_t state = 0x123456789abcdefULL;
    for (int i = 0; i < 4000; i++) {
        state = state * 6364136223846793005ULL + 1;
        uint64_t a = state % ((uint64_t)CSS_MATH_LIMIT + 1);
        state = state * 6364136223846793005ULL + 1;
        uint64_t b = state % ((uint64_t)CSS_MATH_LIMIT + 1);
        state = state * 6364136223846793005ULL + 1;
        uint64_t d = i % 2 ? CSS_MATH_ONE : 1 + state % (uint64_t)CSS_MATH_LIMIT;
        __uint128_t expected = (__uint128_t)a * b / d;
        int bad = 0;
        uint64_t actual = css_math_muldiv(a, b, d, &bad);
        math_check(expected > CSS_MATH_LIMIT ? bad : !bad && actual == (uint64_t)expected,
                   "portable mul/div vs host 128-bit reference");
    }
#endif
}

int main(void) {
    static const struct { const char* text; int px; } good[] = {
        { "0", 0 }, { "12", 12 }, { "+12px", 12 }, { "-.5rem", -8 },
        { " \t.125REM\n", 2 }, { "12.75px", 12 }, { "-.75px", 0 },
        { "1pt", 1 }, { "3pt", 4 }, { "1pc", 16 }, { "1in", 96 },
        { "2.54cm", 96 }, { "25.4mm", 96 }, { "101.6q", 96 },
        { "1em", 20 }, { "1ch", 10 }, { "1ex", 10 }, { "10vw", 80 },
        { "10vh", 60 }, { "10vmin", 60 }, { "10vmax", 80 },
        { "calc(50vh - 1rem)", 284 },
        { "1e1px", 10 }, { "1E-1rem", 1 }, { "1e+1px", 10 },
        { "calc(.5 * 1rem)", 8 }, { "calc(1rem * .5)", 8 },
        { "calc(1rem * (1 - .25))", 12 }, { "calc((1 - .25) * 1rem)", 12 },
        { "calc(.5 * .5 * 1rem)", 4 }, { "calc(1rem / .25)", 64 },
        { "calc(1rem / (1 + 3))", 4 }, { "calc(1rem / -2)", -8 },
        { "calc(-(1rem + 4px))", -20 }, { "calc(+(1rem - 4px))", 12 },
        { "calc(-(-1rem))", 16 }, { "calc(1px - -2px)", 3 },
        { "calc(1px + +2px)", 3 }, { "calc(1px + 2px * 3)", 7 },
        { "calc(24px / 3 / 2)", 4 }, { "calc(24px - 3px - 2px)", 19 },
        { "calc(.75px + .75px)", 1 }, { "calc(.0625rem * 4)", 4 },
        { "calc(.1px * 100)", 10 }, { "calc(.000001rem * 1000000)", 16 },
        { "calc(calc(.5 * 1rem) + 2px)", 10 }, { "CALC(1PX + 1REM)", 17 },
        { "min(9px, 2px, 7px)", 2 }, { "max(1px, 9px, 3px)", 9 },
        { "min(3px)", 3 }, { "max(-3px, -2px)", -2 },
        { "min(20px + 2px, 3rem / 2)", 22 },
        { "clamp(4px, 1px, 8px)", 4 }, { "clamp(4px, 99px, 8px)", 8 },
        { "clamp(4px, 6px, 8px)", 6 }, { "clamp(10px, 5px, 2px)", 10 },
        { "clamp(min(2px, 4px), calc(.5 * 1rem), max(6px, 9px))", 8 },
        { "calc(1rem * min(.5, .25))", 4 }, { "calc(max(2, 3) * 1rem)", 48 },
        { "calc(1rem * clamp(0, .5, 1))", 8 },
        { "calc(2147483647px * 1)", 2147483647 },
        { "calc(2147483647px / 1)", 2147483647 },
        { "calc(2147483647px * .5)", 1073741823 },
        { "calc(2147483647px / 2147483647 * 100)", 100 },
        { "calc(-2147483647px / -1)", 2147483647 },
        { "calc(1000000px * 1000 / 1000000)", 1000 },
        { "calc(0 * 1px)", 0 }, { "calc(0)", 0 }
    };
    static const char* bad[] = {
        "", " ", "auto", "automatic", "inherit", "none", "var(--x)", "env(safe-area-inset-top)",
        "1unknown", "1pxgarbage", "1px garbage", "1px 2px", "1 px", "1.px", ".px", "--1px", "+ 1px",
        "1rem%", "1px!important", "1px;", "1px)", "(1px)", "calc(1px) trailing", "calc(1px))",
        "calc(1px", "calc()", "calc(())", "calc(1px +)", "calc(1px *)", "calc(* 1px)",
        "calc(1px + 2px garbage)", "calc(1px+2px)", "calc(1px+ 2px)", "calc(1px +2px)",
        "calc(1px -2px)", "calc(1px / 0)", "calc(1px / -0)", "calc(1px / (1 - 1))",
        "calc(1px / min(0, 1))", "calc(1px * 1px)", "calc(0px * 1px)", "calc(1px / 1px)",
        "calc(1 / 1px)", "calc(1px + 1)", "calc(0 + 1px)", "calc(1 + 2)", "min(1, 2)",
        "min(1px, 2)", "max(1, 2px)", "calc(1px * var(--x))", "calc(1px * bogus(2))",
        "min()", "max(1px,)", "min(,1px)", "calc(1px, 2px)", "clamp(1px, 2px)",
        "clamp(1px, 2px, 3px, 4px)", "clamp(1px, 2, 3px)", "clamp(1px, 2px,)",
        "min(1px 2px)", "max(1px, 2px", "calc(2147483647px + 1px)",
        "calc(-2147483647px - 1px)", "calc(2147483647px * 2)", "calc(2147483647px / .5)",
        "9999999999999999999999999999999px", "calc(1000000000 * 1000000000 * 1px)",
        "calc(1px / .0000001)", "1e+px", "1e999px", "1e-999px"
    };
    for (unsigned i = 0; i < sizeof(good) / sizeof(good[0]); i++)
        length_case(good[i].text, 20, -1, good[i].px, 0);
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        length_case(bad[i], 20, -1, LEN_AUTO, 0);
    length_case(NULL, 20, -1, LEN_AUTO, 0);
    length_case("50%", 16, -1, LEN_AUTO, 50);
    length_case("12.5%", 16, -1, LEN_AUTO, 12);
    length_case("-25%", 16, -1, LEN_AUTO, -25);
    length_case("50%", 16, 320, 160, 50);
    length_case("12.5%", 16, 320, 40, 12);
    length_case("calc(50% * .5)", 16, -1, LEN_AUTO, 25);
    length_case("min(25%, 50%)", 16, -1, LEN_AUTO, 25);
    length_case("calc(100% - 1rem)", 16, -1, LEN_AUTO, 0);
    length_case("calc(0% + 1px)", 16, -1, LEN_AUTO, 0);
    length_case("min(50%, 20px)", 16, -1, LEN_AUTO, 0);
    length_case("clamp(20px, 50%, 200px)", 16, -1, LEN_AUTO, 0);
    length_case("calc(100% - 1rem)", 16, 320, 304, 0);
    length_case("min(50%, 20px)", 16, 320, 20, 0);
    length_case("max(50%, 20px)", 16, 320, 160, 0);
    length_case("clamp(20px, 50%, 120px)", 16, 320, 120, 0);
    length_case("calc(50% * 1px)", 16, 320, LEN_AUTO, 0);
    length_case("50% garbage", 16, -1, LEN_AUTO, 0);
    math_check(parse_len("25%", 16, -1, NULL) == LEN_AUTO, "percentage with no output pointer");
    char deep[256];
    strcpy(deep, "calc(");
    for (int i = 0; i < 80; i++) strcat(deep, "(");
    strcat(deep, "1px");
    for (int i = 0; i < 81; i++) strcat(deep, ")");
    length_case(deep, 16, -1, LEN_AUTO, 0);
    char wide[6000];
    strcpy(wide, "calc(1px");
    for (int i = 0; i < 600; i++) strcat(wide, " + 1px");
    strcat(wide, ")");
    length_case(wide, 16, -1, LEN_AUTO, 0);
    memset(wide, ' ', sizeof(wide) - 1);
    wide[sizeof(wide) - 1] = 0;
    length_case(wide, 16, -1, LEN_AUTO, 0);
    math_overflow_reference();
    math_styles();
    math_shorthands();
    math_tailwind_fixture();
    printf("css math: %d checks, %d failures\n", math_checks, math_failures);
    return math_failures ? 1 : 0;
}
