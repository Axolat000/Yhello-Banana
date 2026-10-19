/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CSS_MATH_H
#define CSS_MATH_H

/* Private to css.c. Six decimal places are retained until the final px
 * conversion (towards zero). Explicit 64-bit arithmetic also works on i386;
 * no floating point, long-width assumptions, allocation or __int128. */
#define CSS_MATH_ONE 1000000LL
#define CSS_MATH_LIMIT (2147483647LL * CSS_MATH_ONE)
#define CSS_MATH_DEPTH 32
#define CSS_MATH_STEPS 512

enum { CSS_MATH_NUMBER, CSS_MATH_LENGTH, CSS_MATH_PERCENT };
typedef struct { int64_t n; int dim; } css_math_value;
typedef struct {
    const char* s;
    int font_px, pct_of, depth, steps, bad, percent;
} css_math_parser;

static void css_math_ws(css_math_parser* p) {
    while (is_ws(*p->s)) p->s++;
}

static uint64_t css_math_abs(int64_t n) {
    return (uint64_t)(n < 0 ? -n : n); /* all values bounded by LIMIT */
}

/* floor(a*b/d), bounded by LIMIT even when a*b would overflow uint64_t.
 * Long division keeps the remainder below d and the quotient below LIMIT. */
static uint64_t css_math_muldiv(uint64_t a, uint64_t b, uint64_t d, int* bad) {
    const uint64_t limit = (uint64_t)CSS_MATH_LIMIT;
    if (!d) { *bad = 1; return 0; }
    if (!a || !b) return 0;
    if (b <= 0xffffffffffffffffULL / a) {
        uint64_t q = a * b / d;
        if (q > limit) { *bad = 1; return 0; }
        return q;
    }
    uint64_t whole = a / d, part = a % d, q = 0, r = 0;
    for (int bit = 63; bit >= 0; bit--) {
        uint64_t add = (b >> bit) & 1 ? whole : 0;
        r = r * 2 + (((b >> bit) & 1) ? part : 0);
        add += r / d;
        r %= d;
        if (add > limit || q > (limit - add) / 2) { *bad = 1; return 0; }
        q = q * 2 + add;
    }
    return q;
}

static int64_t css_math_scale(int64_t n, int64_t by, uint64_t div, int* bad) {
    uint64_t q = css_math_muldiv(css_math_abs(n), css_math_abs(by), div, bad);
    return (n < 0) != (by < 0) ? -(int64_t)q : (int64_t)q;
}

static css_math_value css_math_fail(css_math_parser* p) {
    css_math_value v = { 0, CSS_MATH_NUMBER };
    p->bad = 1;
    return v;
}

static int css_math_digit(char c) { return c >= '0' && c <= '9'; }

/* Unsigned number and optional unit. Unary signs belong to calc_factor. */
static css_math_value len_term(css_math_parser* p) {
    css_math_value v = { 0, CSS_MATH_NUMBER };
    int digits = 0;
    while (css_math_digit(*p->s)) {
        int d = *p->s++ - '0';
        if (v.n > (CSS_MATH_LIMIT - d * CSS_MATH_ONE) / 10)
            return css_math_fail(p);
        v.n = v.n * 10 + d * CSS_MATH_ONE;
        digits++;
    }
    if (*p->s == '.') {
        int64_t place = CSS_MATH_ONE / 10;
        p->s++;
        if (!css_math_digit(*p->s)) return css_math_fail(p);
        while (css_math_digit(*p->s)) {
            v.n += (*p->s++ - '0') * place;
            place /= 10;
            digits++;
        }
        if (v.n > CSS_MATH_LIMIT) return css_math_fail(p);
    }
    if (!digits) return css_math_fail(p);
    if ((*p->s == 'e' || *p->s == 'E') &&
        (css_math_digit(p->s[1]) || p->s[1] == '+' || p->s[1] == '-')) {
        int exp = 0, neg;
        p->s++;
        neg = *p->s == '-';
        if (*p->s == '+' || *p->s == '-') p->s++;
        if (!css_math_digit(*p->s)) return css_math_fail(p);
        while (css_math_digit(*p->s)) {
            exp = exp * 10 + (*p->s++ - '0');
            if (exp > 64) return css_math_fail(p);
        }
        while (exp--) {
            if (neg) v.n /= 10;
            else {
                if (v.n > CSS_MATH_LIMIT / 10) return css_math_fail(p);
                v.n *= 10;
            }
        }
    }
    const char* unit = p->s;
    while ((*p->s >= 'a' && *p->s <= 'z') || (*p->s >= 'A' && *p->s <= 'Z')) p->s++;
    int nunit = (int)(p->s - unit);
    int64_t by = 1;
    uint64_t div = 1;
    if (nunit) {
        v.dim = CSS_MATH_LENGTH;
#define CSS_MATH_UNIT(name) (nunit == (int)(sizeof(name) - 1) && strncasecmp(unit, name, sizeof(name) - 1) == 0)
        if (CSS_MATH_UNIT("px")) {}
        else if (CSS_MATH_UNIT("rem")) by = 16;
        else if (CSS_MATH_UNIT("em")) by = p->font_px;
        else if (CSS_MATH_UNIT("pt")) { by = 4; div = 3; }
        else if (CSS_MATH_UNIT("pc")) by = 16;
        else if (CSS_MATH_UNIT("in")) by = 96;
        else if (CSS_MATH_UNIT("cm")) { by = 4800; div = 127; }
        else if (CSS_MATH_UNIT("mm")) { by = 480; div = 127; }
        else if (CSS_MATH_UNIT("q")) { by = 120; div = 127; }
        else if (CSS_MATH_UNIT("vw")) { by = css_viewport_w; div = 100; }
        else if (CSS_MATH_UNIT("vh")) { by = css_viewport_h; div = 100; }
        else if (CSS_MATH_UNIT("vmin")) { by = css_viewport_w < css_viewport_h ? css_viewport_w : css_viewport_h; div = 100; }
        else if (CSS_MATH_UNIT("vmax")) { by = css_viewport_w > css_viewport_h ? css_viewport_w : css_viewport_h; div = 100; }
        else if (CSS_MATH_UNIT("ch") || CSS_MATH_UNIT("ex")) { by = p->font_px; div = 2; }
        else return css_math_fail(p);
#undef CSS_MATH_UNIT
    } else if (*p->s == '%') {
        p->s++;
        p->percent = (int)(v.n / CSS_MATH_ONE);
        v.dim = CSS_MATH_PERCENT;
        if (p->pct_of >= 0) { v.dim = CSS_MATH_LENGTH; by = p->pct_of; div = 100; }
    }
    v.n = css_math_scale(v.n, by, div, &p->bad);
    return v;
}

static css_math_value calc_expr(css_math_parser* p);

static css_math_value calc_factor(css_math_parser* p) {
    if (++p->depth > CSS_MATH_DEPTH || ++p->steps > CSS_MATH_STEPS)
        return css_math_fail(p);
    css_math_ws(p);
    int neg = 0;
    while (*p->s == '+' || *p->s == '-') {
        if (++p->steps > CSS_MATH_STEPS) return css_math_fail(p);
        if (*p->s++ == '-') neg = !neg;
        css_math_ws(p);
    }
    css_math_value v;
    int fn = 0; /* 1 calc/group, 2 min, 3 max, 4 clamp */
    if (*p->s == '(') { fn = 1; p->s++; }
    else if (strncasecmp(p->s, "calc(", 5) == 0) { fn = 1; p->s += 5; }
    else if (strncasecmp(p->s, "min(", 4) == 0) { fn = 2; p->s += 4; }
    else if (strncasecmp(p->s, "max(", 4) == 0) { fn = 3; p->s += 4; }
    else if (strncasecmp(p->s, "clamp(", 6) == 0) { fn = 4; p->s += 6; }
    if (!fn) v = len_term(p);
    else {
        v = calc_expr(p);
        int count = 1;
        int64_t lower = v.n;
        css_math_ws(p);
        while (!p->bad && *p->s == ',') {
            if (fn == 1 || (fn == 4 && count == 3)) return css_math_fail(p);
            p->s++;
            css_math_value r = calc_expr(p);
            if (v.dim != r.dim) return css_math_fail(p);
            count++;
            if ((fn == 2 && r.n < v.n) || (fn == 3 && r.n > v.n) || (fn == 4 && count == 2)) v.n = r.n;
            if (fn == 4 && count == 3) {
                if (v.n > r.n) v.n = r.n;
                if (v.n < lower) v.n = lower; /* min wins if min > max */
            }
            css_math_ws(p);
        }
        if (p->bad || *p->s != ')' || (fn == 4 && count != 3)) return css_math_fail(p);
        p->s++;
    }
    if (neg) v.n = -v.n;
    p->depth--;
    return v;
}

static css_math_value calc_term(css_math_parser* p) {
    css_math_value v = calc_factor(p);
    while (!p->bad) {
        const char* s = p->s;
        while (is_ws(*s)) s++;
        if (*s != '*' && *s != '/') break;
        char op = *s++;
        p->s = s;
        css_math_value r = calc_factor(p);
        if (p->bad) break;
        if (op == '*') {
            if (v.dim != CSS_MATH_NUMBER && r.dim != CSS_MATH_NUMBER) return css_math_fail(p);
            v.n = css_math_scale(v.n, r.n, CSS_MATH_ONE, &p->bad);
            if (v.dim == CSS_MATH_NUMBER) v.dim = r.dim;
        } else {
            if (r.dim != CSS_MATH_NUMBER || !r.n) return css_math_fail(p);
            v.n = css_math_scale(v.n, r.n < 0 ? -CSS_MATH_ONE : CSS_MATH_ONE, css_math_abs(r.n), &p->bad);
        }
    }
    return v;
}

static css_math_value calc_expr(css_math_parser* p) {
    css_math_value v = calc_term(p);
    while (!p->bad) {
        const char* s = p->s;
        while (is_ws(*s)) s++;
        if (*s != '+' && *s != '-') break;
        /* CSS binary + and - require whitespace on both sides. */
        if (s == p->s || !is_ws(s[1])) return css_math_fail(p);
        char op = *s++;
        p->s = s;
        css_math_value r = calc_term(p);
        /* Unknown-base percentages cannot be combined with px honestly. */
        if (p->bad || v.dim != r.dim) return css_math_fail(p);
        int64_t n = op == '+' ? v.n + r.n : v.n - r.n;
        if (n < -CSS_MATH_LIMIT || n > CSS_MATH_LIMIT) return css_math_fail(p);
        v.n = n;
    }
    return v;
}

#endif
