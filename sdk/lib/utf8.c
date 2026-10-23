/* SPDX-License-Identifier: BSD-2-Clause */
/* Restartable UTF-8 conversion. Keep state in the first word so older
 * four-byte C mbstate_t clients remain compatible. */
#include <wchar.h>
#include <errno.h>
#ifdef __x86_64__
static __thread mbstate_t read_state, write_state;
#else
static mbstate_t read_state, write_state;
#endif
size_t mbrtowc(wchar_t* out,const char* s,size_t n,mbstate_t* ps) {
    if(!ps)ps=&read_state;
    if(!s){s="";n=1;out=0;}
    if(!n)return (size_t)-2;
    unsigned state=ps->__state,cp=state&0x1fffff,remaining=(state>>21)&3,total=(state>>23)&3;
    size_t i=0;
    if(!remaining) {
        unsigned c=(unsigned char)s[i++];
        if(c<0x80){ps->__state=0;if(out)*out=c;return c?1:0;}
        if(c>=0xc2&&c<=0xdf){remaining=total=1;cp=c&31;}
        else if(c>=0xe0&&c<=0xef){remaining=total=2;cp=c&15;}
        else if(c>=0xf0&&c<=0xf4){remaining=total=3;cp=c&7;}
        else goto invalid;
    }
    while(remaining&&i<n){
        unsigned c=(unsigned char)s[i++];if((c&0xc0)!=0x80)goto invalid;
        cp=(cp<<6)|(c&63);remaining--;
    }
    if(remaining){ps->__state=cp|(remaining<<21)|(total<<23);return (size_t)-2;}
    if(!total||cp<(total==1?0x80:total==2?0x800:0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))goto invalid;
    ps->__state=0;if(out)*out=(wchar_t)cp;return i;
invalid:
    ps->__state=0;errno=EILSEQ;return (size_t)-1;
}
size_t wcrtomb(char* s,wchar_t wc,mbstate_t* ps){
    if(!ps)ps=&write_state;
    ps->__state=0;
    if(!s)return 1; /* equivalent to encoding L'\0' into an internal buffer */
    unsigned v=(unsigned)wc;
    if(v>=0xd800&&v<=0xdfff){errno=EILSEQ;return (size_t)-1;}
    if(v<0x80){s[0]=v;return 1;}
    if(v<0x800){s[0]=0xc0|(v>>6);s[1]=0x80|(v&63);return 2;}
    if(v<0x10000){s[0]=0xe0|(v>>12);s[1]=0x80|((v>>6)&63);s[2]=0x80|(v&63);return 3;}
    if(v<=0x10ffff){s[0]=0xf0|(v>>18);s[1]=0x80|((v>>12)&63);s[2]=0x80|((v>>6)&63);s[3]=0x80|(v&63);return 4;}
    errno=EILSEQ;return (size_t)-1;
}
int mbsinit(const mbstate_t* ps){return !ps||!ps->__state;}

/* --- wide character classes and widths (<wctype.h>, wcwidth) --- */
#include <wctype.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

struct wrange { unsigned lo, hi; };
static int in_ranges(unsigned c, const struct wrange* r, int n) {
    int lo = 0, hi = n - 1;
    if (c < r[0].lo || c > r[n - 1].hi) return 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (c > r[mid].hi) lo = mid + 1;
        else if (c < r[mid].lo) hi = mid - 1;
        else return 1;
    }
    return 0;
}

/* characters that take no column of their own (combining marks,
 * zero-width joiners, variation selectors) */
static const struct wrange zero_width[] = {
    {0x0300,0x036F},{0x0483,0x0489},{0x0591,0x05BD},{0x05BF,0x05BF},{0x05C1,0x05C2},
    {0x05C4,0x05C5},{0x05C7,0x05C7},{0x0610,0x061A},{0x064B,0x065F},{0x0670,0x0670},
    {0x06D6,0x06DC},{0x06DF,0x06E4},{0x06E7,0x06E8},{0x06EA,0x06ED},{0x0711,0x0711},
    {0x0730,0x074A},{0x07A6,0x07B0},{0x07EB,0x07F3},{0x0816,0x0819},{0x081B,0x0823},
    {0x0825,0x0827},{0x0829,0x082D},{0x0859,0x085B},{0x08D3,0x08E1},{0x08E3,0x0902},
    {0x093A,0x093A},{0x093C,0x093C},{0x0941,0x0948},{0x094D,0x094D},{0x0951,0x0957},
    {0x0962,0x0963},{0x0981,0x0981},{0x09BC,0x09BC},{0x09C1,0x09C4},{0x09CD,0x09CD},
    {0x09E2,0x09E3},{0x0A01,0x0A02},{0x0A3C,0x0A3C},{0x0A41,0x0A51},{0x0A70,0x0A71},
    {0x0A75,0x0A75},{0x0A81,0x0A82},{0x0ABC,0x0ABC},{0x0AC1,0x0AC8},{0x0ACD,0x0ACD},
    {0x0AE2,0x0AE3},{0x0B01,0x0B01},{0x0B3C,0x0B3C},{0x0B3F,0x0B3F},{0x0B41,0x0B44},
    {0x0B4D,0x0B4D},{0x0B56,0x0B56},{0x0B62,0x0B63},{0x0B82,0x0B82},{0x0BC0,0x0BC0},
    {0x0BCD,0x0BCD},{0x0C00,0x0C00},{0x0C3E,0x0C40},{0x0C46,0x0C56},{0x0C62,0x0C63},
    {0x0CBC,0x0CBC},{0x0CCC,0x0CCD},{0x0CE2,0x0CE3},{0x0D00,0x0D01},{0x0D41,0x0D44},
    {0x0D4D,0x0D4D},{0x0D62,0x0D63},{0x0DCA,0x0DCA},{0x0DD2,0x0DD6},{0x0E31,0x0E31},
    {0x0E34,0x0E3A},{0x0E47,0x0E4E},{0x0EB1,0x0EB1},{0x0EB4,0x0EBC},{0x0EC8,0x0ECD},
    {0x0F18,0x0F19},{0x0F35,0x0F35},{0x0F37,0x0F37},{0x0F39,0x0F39},{0x0F71,0x0F7E},
    {0x0F80,0x0F84},{0x0F86,0x0F87},{0x0F8D,0x0FBC},{0x0FC6,0x0FC6},{0x102D,0x1030},
    {0x1032,0x1037},{0x1039,0x103A},{0x103D,0x103E},{0x1058,0x1059},{0x105E,0x1060},
    {0x1071,0x1074},{0x1082,0x1082},{0x1085,0x1086},{0x108D,0x108D},{0x109D,0x109D},
    {0x1160,0x11FF},{0x135D,0x135F},{0x1712,0x1714},{0x1732,0x1734},{0x1752,0x1753},
    {0x1772,0x1773},{0x17B4,0x17B5},{0x17B7,0x17BD},{0x17C6,0x17C6},{0x17C9,0x17D3},
    {0x17DD,0x17DD},{0x180B,0x180F},{0x1885,0x1886},{0x18A9,0x18A9},{0x1920,0x1922},
    {0x1927,0x1928},{0x1932,0x1932},{0x1939,0x193B},{0x1A17,0x1A18},{0x1A1B,0x1A1B},
    {0x1A56,0x1A56},{0x1A58,0x1A60},{0x1A62,0x1A62},{0x1A65,0x1A6C},{0x1A73,0x1A7F},
    {0x1AB0,0x1AFF},{0x1B00,0x1B03},{0x1B34,0x1B34},{0x1B36,0x1B3A},{0x1B3C,0x1B3C},
    {0x1B42,0x1B42},{0x1B6B,0x1B73},{0x1B80,0x1B81},{0x1BA2,0x1BA5},{0x1BA8,0x1BA9},
    {0x1BAB,0x1BAD},{0x1BE6,0x1BE6},{0x1BE8,0x1BE9},{0x1BED,0x1BED},{0x1BEF,0x1BF1},
    {0x1C2C,0x1C33},{0x1C36,0x1C37},{0x1CD0,0x1CD2},{0x1CD4,0x1CE0},{0x1CE2,0x1CE8},
    {0x1CED,0x1CED},{0x1CF4,0x1CF4},{0x1CF8,0x1CF9},{0x1DC0,0x1DFF},{0x200B,0x200F},
    {0x202A,0x202E},{0x2060,0x2064},{0x20D0,0x20F0},{0x2CEF,0x2CF1},{0x2D7F,0x2D7F},
    {0x2DE0,0x2DFF},{0x302A,0x302D},{0x3099,0x309A},{0xA66F,0xA672},{0xA674,0xA67D},
    {0xA69E,0xA69F},{0xA6F0,0xA6F1},{0xA802,0xA802},{0xA806,0xA806},{0xA80B,0xA80B},
    {0xA825,0xA826},{0xA8C4,0xA8C5},{0xA8E0,0xA8F1},{0xA8FF,0xA8FF},{0xA926,0xA92D},
    {0xA947,0xA951},{0xA980,0xA982},{0xA9B3,0xA9B3},{0xA9B6,0xA9B9},{0xA9BC,0xA9BD},
    {0xA9E5,0xA9E5},{0xAA29,0xAA2E},{0xAA31,0xAA32},{0xAA35,0xAA36},{0xAA43,0xAA43},
    {0xAA4C,0xAA4C},{0xAA7C,0xAA7C},{0xAAB0,0xAAB0},{0xAAB2,0xAAB4},{0xAAB7,0xAAB8},
    {0xAABE,0xAABF},{0xAAC1,0xAAC1},{0xAAEC,0xAAED},{0xAAF6,0xAAF6},{0xABE5,0xABE5},
    {0xABE8,0xABE8},{0xABED,0xABED},{0xD7B0,0xD7FF},{0xFB1E,0xFB1E},{0xFE00,0xFE0F},
    {0xFE20,0xFE2F},{0xFEFF,0xFEFF},{0xFFF9,0xFFFB},{0x101FD,0x101FD},{0x10A01,0x10A0F},
    {0x10A38,0x10A3F},{0x11001,0x11001},{0x11038,0x11046},{0x1107F,0x11081},
    {0x1D167,0x1D169},{0x1D173,0x1D182},{0x1D185,0x1D18B},{0x1D1AA,0x1D1AD},
    {0x1E000,0x1E02A},{0x1E8D0,0x1E8D6},{0x1E944,0x1E94A},{0x1F3FB,0x1F3FF},
    {0xE0001,0xE0001},{0xE0020,0xE007F},{0xE0100,0xE01EF},
};

/* East Asian wide and fullwidth characters, and emoji presentation */
static const struct wrange wide[] = {
    {0x1100,0x115F},{0x231A,0x231B},{0x2329,0x232A},{0x23E9,0x23EC},{0x23F0,0x23F0},
    {0x23F3,0x23F3},{0x25FD,0x25FE},{0x2614,0x2615},{0x2648,0x2653},{0x267F,0x267F},
    {0x2693,0x2693},{0x26A1,0x26A1},{0x26AA,0x26AB},{0x26BD,0x26BE},{0x26C4,0x26C5},
    {0x26CE,0x26CE},{0x26D4,0x26D4},{0x26EA,0x26EA},{0x26F2,0x26F3},{0x26F5,0x26F5},
    {0x26FA,0x26FA},{0x26FD,0x26FD},{0x2705,0x2705},{0x270A,0x270B},{0x2728,0x2728},
    {0x274C,0x274C},{0x274E,0x274E},{0x2753,0x2755},{0x2757,0x2757},{0x2795,0x2797},
    {0x27B0,0x27B0},{0x27BF,0x27BF},{0x2B1B,0x2B1C},{0x2B50,0x2B50},{0x2B55,0x2B55},
    {0x2E80,0x303E},{0x3041,0x3247},{0x3250,0x4DBF},{0x4E00,0xA4C6},{0xA960,0xA97C},
    {0xAC00,0xD7A3},{0xF900,0xFAFF},{0xFE10,0xFE19},{0xFE30,0xFE6B},{0xFF00,0xFF60},
    {0xFFE0,0xFFE6},{0x16FE0,0x16FE4},{0x17000,0x18CFF},{0x1B000,0x1B2FF},
    {0x1F004,0x1F004},{0x1F0CF,0x1F0CF},{0x1F18E,0x1F18E},{0x1F191,0x1F19A},
    {0x1F200,0x1F202},{0x1F210,0x1F23B},{0x1F240,0x1F248},{0x1F250,0x1F251},
    {0x1F260,0x1F265},{0x1F300,0x1F320},{0x1F32D,0x1F335},{0x1F337,0x1F37C},
    {0x1F37E,0x1F393},{0x1F3A0,0x1F3CA},{0x1F3CF,0x1F3D3},{0x1F3E0,0x1F3F0},
    {0x1F3F4,0x1F3F4},{0x1F3F8,0x1F3FA},{0x1F400,0x1F43E},{0x1F440,0x1F440},
    {0x1F442,0x1F4FC},{0x1F4FF,0x1F53D},{0x1F54B,0x1F54E},{0x1F550,0x1F567},
    {0x1F57A,0x1F57A},{0x1F595,0x1F596},{0x1F5A4,0x1F5A4},{0x1F5FB,0x1F64F},
    {0x1F680,0x1F6C5},{0x1F6CC,0x1F6CC},{0x1F6D0,0x1F6D2},{0x1F6D5,0x1F6D7},
    {0x1F6DC,0x1F6DF},{0x1F6EB,0x1F6EC},{0x1F6F4,0x1F6FC},{0x1F7E0,0x1F7EB},
    {0x1F7F0,0x1F7F0},{0x1F90C,0x1F93A},{0x1F93C,0x1F945},{0x1F947,0x1F9FF},
    {0x1FA70,0x1FAFF},{0x20000,0x2FFFD},{0x30000,0x3FFFD},
};

int wcwidth(wchar_t wc) {
    unsigned c = (unsigned)wc;
    if (c == 0) return 0;
    if (c < 32 || (c >= 0x7F && c < 0xA0)) return -1;
    if (c < 0x300) return 1;
    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return -1;
    if (in_ranges(c, zero_width, sizeof zero_width / sizeof *zero_width)) return 0;
    if (in_ranges(c, wide, sizeof wide / sizeof *wide)) return 2;
    return 1;
}

int wcswidth(const wchar_t* s, size_t n) {
    int w = 0;
    for (size_t i = 0; i < n && s[i]; i++) {
        int k = wcwidth(s[i]);
        if (k < 0) return -1;
        w += k;
    }
    return w;
}

/* Unicode spaces and punctuation outside ASCII (the main blocks) */
static const struct wrange uspace[] = {
    {0x1680,0x1680},{0x2000,0x2006},{0x2008,0x200A},{0x2028,0x2029},{0x205F,0x205F},{0x3000,0x3000},
};
static const struct wrange upunct[] = {
    {0x00A1,0x00BF},{0x00D7,0x00D7},{0x00F7,0x00F7},{0x02C2,0x02C5},{0x02D2,0x02DF},
    {0x037E,0x037E},{0x0387,0x0387},{0x055A,0x055F},{0x0589,0x058A},{0x05BE,0x05BE},
    {0x060C,0x060D},{0x061B,0x061F},{0x066A,0x066D},{0x06D4,0x06D4},{0x0964,0x0965},
    {0x0E4F,0x0E4F},{0x2010,0x2027},{0x2030,0x205E},{0x207A,0x207E},{0x208A,0x208E},
    {0x20A0,0x20CF},{0x2100,0x2101},{0x2103,0x2106},{0x2108,0x2109},{0x2114,0x2114},
    {0x2116,0x2118},{0x211E,0x2123},{0x2125,0x2125},{0x2127,0x2127},{0x2129,0x2129},
    {0x2190,0x245F},{0x2500,0x2775},{0x2794,0x2BFF},{0x2E00,0x2E7F},{0x3001,0x3004},
    {0x3008,0x3020},{0x3030,0x3030},{0x303D,0x303D},{0x30A0,0x30A0},{0x30FB,0x30FB},
    {0xFE10,0xFE19},{0xFE30,0xFE6B},{0xFF01,0xFF0F},{0xFF1A,0xFF20},{0xFF3B,0xFF40},
    {0xFF5B,0xFF65},{0xFFE0,0xFFEE},{0x1F000,0x1FAFF},
};
static const struct wrange udigit_like[] = {
    {0x0660,0x0669},{0x06F0,0x06F9},{0x0966,0x096F},{0xFF10,0xFF19},
};

int iswspace(wint_t c) {
    if (c < 0x80) return isspace((int)c);
    return c == 0x85 || c == 0xA0 ? 0 : in_ranges(c, uspace, sizeof uspace / sizeof *uspace);
}
int iswblank(wint_t c) { return c == ' ' || c == '\t' || (c >= 0x80 && c != 0x2028 && c != 0x2029 && iswspace(c)); }
int iswcntrl(wint_t c) { return c < 32 || (c >= 0x7F && c < 0xA0) || c == 0x2028 || c == 0x2029; }
int iswdigit(wint_t c) { return c >= '0' && c <= '9'; }
int iswxdigit(wint_t c) { return c < 0x80 && isxdigit((int)c); }
int iswprint(wint_t c) {
    if (c < 0x80) return isprint((int)c);
    if (iswcntrl(c) || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF) || c == 0xFFFE || c == 0xFFFF) return 0;
    return 1;
}
int iswgraph(wint_t c) { return iswprint(c) && !iswspace(c) && c != 0xA0; }
int iswpunct(wint_t c) {
    if (c < 0x80) return ispunct((int)c);
    return in_ranges(c, upunct, sizeof upunct / sizeof *upunct);
}
/* a letter: printable, not a space, punctuation, digit or combining mark */
int iswalpha(wint_t c) {
    if (c < 0x80) return isalpha((int)c);
    if (c == 0xAA || c == 0xB5 || c == 0xBA) return 1;
    return iswgraph(c) && !iswpunct(c) && !in_ranges(c, udigit_like, sizeof udigit_like / sizeof *udigit_like)
        && wcwidth((wchar_t)c) != 0;
}
int iswalnum(wint_t c) { return iswdigit(c) || iswalpha(c); }

/* case: Latin-1, Latin Extended-A/B pairs, Greek, Cyrillic, Armenian, fullwidth */
wint_t towlower(wint_t c) {
    if (c < 0x80) return (wint_t)tolower((int)c);
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;
    if (c >= 0x100 && c <= 0x17F) {
        if (c == 0x130) return 'i';
        if (c == 0x178) return 0xFF;
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c + 1 : c;
        if (c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F) return c;
        return (c & 1) ? c : c + 1;
    }
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
    if (c == 0x386) return 0x3AC;
    if (c >= 0x388 && c <= 0x38A) return c + 37;
    if (c == 0x38C) return 0x3CC;
    if (c == 0x38E || c == 0x38F) return c + 63;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    if (((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF) || (c >= 0x4D0 && c <= 0x52F)) && !(c & 1)) return c + 1;
    if (c >= 0x531 && c <= 0x556) return c + 48;
    if (c >= 0x1E00 && c <= 0x1EFF && !(c & 1) && !(c >= 0x1E96 && c <= 0x1E9F)) return c + 1;
    if (c >= 0xFF21 && c <= 0xFF3A) return c + 32;
    return c;
}
wint_t towupper(wint_t c) {
    if (c < 0x80) return (wint_t)toupper((int)c);
    if (c == 0xB5) return 0x39C;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x17F) {
        if (c == 0x131) return 'I';
        if (c == 0x17F) return 'S';
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c : c - 1;
        if (c == 0x130 || c == 0x138 || c == 0x149) return c;
        return (c & 1) ? c - 1 : c;
    }
    if (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) return c - 32;
    if (c == 0x3C2) return 0x3A3;
    if (c == 0x3AC) return 0x386;
    if (c >= 0x3AD && c <= 0x3AF) return c - 37;
    if (c == 0x3CC) return 0x38C;
    if (c == 0x3CD || c == 0x3CE) return c - 63;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    if (((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF) || (c >= 0x4D0 && c <= 0x52F)) && (c & 1)) return c - 1;
    if (c >= 0x561 && c <= 0x586) return c - 48;
    if (c >= 0x1E00 && c <= 0x1EFF && (c & 1) && !(c >= 0x1E96 && c <= 0x1E9F)) return c - 1;
    if (c >= 0xFF41 && c <= 0xFF5A) return c - 32;
    return c;
}
int iswlower(wint_t c) { return towupper(c) != c || (c < 0x80 && islower((int)c)) || c == 0xDF; }
int iswupper(wint_t c) { return towlower(c) != c; }

static const char* const wclass[] = {
    "alnum", "alpha", "blank", "cntrl", "digit", "graph", "lower", "print", "punct", "space", "upper", "xdigit",
};
static int (*const wclassf[])(wint_t) = {
    iswalnum, iswalpha, iswblank, iswcntrl, iswdigit, iswgraph, iswlower, iswprint, iswpunct, iswspace, iswupper, iswxdigit,
};
wctype_t wctype(const char* name) {
    for (unsigned i = 0; i < sizeof wclass / sizeof *wclass; i++)
        if (!strcmp(name, wclass[i])) return i + 1;
    return 0;
}
int iswctype(wint_t c, wctype_t t) {
    return t >= 1 && t <= sizeof wclass / sizeof *wclass ? wclassf[t - 1](c) != 0 : 0;
}
static const int trans_lower = 1, trans_upper = 2;
wctrans_t wctrans(const char* name) {
    return !strcmp(name, "tolower") ? &trans_lower : !strcmp(name, "toupper") ? &trans_upper : 0;
}
wint_t towctrans(wint_t c, wctrans_t t) {
    return t == &trans_lower ? towlower(c) : t == &trans_upper ? towupper(c) : c;
}

wchar_t* wcsdup(const wchar_t* s) {
    size_t n = (wcslen(s) + 1) * sizeof(wchar_t);
    wchar_t* d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}
size_t wcsnlen(const wchar_t* s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}
int wcsncasecmp(const wchar_t* a, const wchar_t* b, size_t n) {
    for (; n; n--, a++, b++) {
        wint_t x = towlower((wint_t)*a), y = towlower((wint_t)*b);
        if (x != y) return x < y ? -1 : 1;
        if (!x) break;
    }
    return 0;
}
int wcscasecmp(const wchar_t* a, const wchar_t* b) { return wcsncasecmp(a, b, (size_t)-1); }

/* ── iconv: UTF-8, ASCII, Latin-1, UTF-16, UTF-32 (<iconv.h>) ───────── */
#include <iconv.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { E_UTF8, E_ASCII, E_LATIN1, E_UTF16, E_UTF16LE, E_UTF16BE, E_UTF32, E_UTF32LE, E_UTF32BE };
typedef struct { int from, to, ignore, translit, bom_done, in_le; } ic_t;

static int ic_enc(const char* name, int* ignore, int* translit) {
    char n[32];
    size_t k = 0;
    for (const char* p = name; *p && k + 1 < sizeof(n); p++) {
        if (p[0] == '/' && p[1] == '/') {
            if (!strncasecmp(p + 2, "IGNORE", 6)) *ignore = 1;
            if (!strncasecmp(p + 2, "TRANSLIT", 8)) *translit = 1;
            break;
        }
        if (*p != '-' && *p != '_') n[k++] = (char)toupper((unsigned char)*p);
    }
    n[k] = 0;
    if (!k || !strcmp(n, "UTF8")) return E_UTF8;                   /* ("": the locale's - UTF-8) */
    if (!strcmp(n, "ASCII") || !strcmp(n, "USASCII") || !strcmp(n, "ANSIX3.41968") || !strcmp(n, "646")) return E_ASCII;
    if (!strcmp(n, "ISO88591") || !strcmp(n, "LATIN1") || !strcmp(n, "L1") || !strcmp(n, "ISO885911987")) return E_LATIN1;
    if (!strcmp(n, "UTF16") || !strcmp(n, "UCS2")) return E_UTF16;
    if (!strcmp(n, "UTF16LE") || !strcmp(n, "UCS2LE")) return E_UTF16LE;
    if (!strcmp(n, "UTF16BE") || !strcmp(n, "UCS2BE")) return E_UTF16BE;
    if (!strcmp(n, "UTF32") || !strcmp(n, "UCS4")) return E_UTF32;
    if (!strcmp(n, "UTF32LE") || !strcmp(n, "UCS4LE") || !strcmp(n, "WCHART")) return E_UTF32LE;
    if (!strcmp(n, "UTF32BE") || !strcmp(n, "UCS4BE")) return E_UTF32BE;
    return -1;
}

iconv_t iconv_open(const char* tocode, const char* fromcode) {
    int ig = 0, tr = 0, dummy = 0;
    int to = ic_enc(tocode, &ig, &tr), from = ic_enc(fromcode, &dummy, &dummy);
    if (to < 0 || from < 0) { errno = EINVAL; return (iconv_t)-1; }
    ic_t* c = (ic_t*)calloc(1, sizeof(ic_t));
    if (!c) { errno = ENOMEM; return (iconv_t)-1; }
    c->from = from; c->to = to; c->ignore = ig; c->translit = tr;
    c->in_le = from == E_UTF16LE || from == E_UTF32LE;
    return (iconv_t)c;
}

int iconv_close(iconv_t cd) {
    if (cd == (iconv_t)-1 || !cd) { errno = EBADF; return -1; }
    free(cd);
    return 0;
}

static unsigned rd16(const unsigned char* p, int le) { return le ? p[0] | p[1] << 8 : p[0] << 8 | p[1]; }
static unsigned long rd32(const unsigned char* p, int le) {
    return le ? (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24
              : (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* one character from in: its length (0: incomplete; -1: invalid) */
static int ic_get(ic_t* c, const unsigned char* s, size_t n, unsigned long* cp) {
    switch (c->from) {
    case E_ASCII: if (s[0] > 0x7F) return -1; *cp = s[0]; return 1;
    case E_LATIN1: *cp = s[0]; return 1;
    case E_UTF8: {
        mbstate_t st;
        wchar_t w;
        memset(&st, 0, sizeof(st));
        size_t r = mbrtowc(&w, (const char*)s, n, &st);
        if (r == (size_t)-2) return 0;
        if (r == (size_t)-1) return -1;
        *cp = (unsigned long)w;
        return r ? (int)r : 1;
    }
    case E_UTF16: case E_UTF16LE: case E_UTF16BE: {
        if (n < 2) return 0;
        int le = c->in_le;
        if (c->from == E_UTF16 && !c->bom_done) {
            c->bom_done = 1;
            if (s[0] == 0xFF && s[1] == 0xFE) { c->in_le = 1; return -2; }
            if (s[0] == 0xFE && s[1] == 0xFF) { c->in_le = 0; return -2; }
        }
        unsigned u = rd16(s, le);
        if (u >= 0xDC00 && u <= 0xDFFF) return -1;
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (n < 4) return 0;
            unsigned v = rd16(s + 2, le);
            if (v < 0xDC00 || v > 0xDFFF) return -1;
            *cp = 0x10000 + ((unsigned long)(u - 0xD800) << 10) + (v - 0xDC00);
            return 4;
        }
        *cp = u;
        return 2;
    }
    default: {
        if (n < 4) return 0;
        if (c->from == E_UTF32 && !c->bom_done) {
            c->bom_done = 1;
            if (s[0] == 0xFF && s[1] == 0xFE && !s[2] && !s[3]) { c->in_le = 1; return -2; }
            if (!s[0] && !s[1] && s[2] == 0xFE && s[3] == 0xFF) { c->in_le = 0; return -2; }
        }
        unsigned long u = rd32(s, c->in_le);
        if (u > 0x10FFFF || (u >= 0xD800 && u <= 0xDFFF)) return -1;
        *cp = u;
        return 4;
    }
    }
}

/* one character to out: its length (0: no room; -1: not in that encoding) */
static int ic_put(ic_t* c, unsigned long cp, unsigned char* o, size_t n) {
    switch (c->to) {
    case E_ASCII: if (cp > 0x7F) return -1; if (!n) return 0; o[0] = (unsigned char)cp; return 1;
    case E_LATIN1: if (cp > 0xFF) return -1; if (!n) return 0; o[0] = (unsigned char)cp; return 1;
    case E_UTF8: {
        char b[4];
        mbstate_t st;
        memset(&st, 0, sizeof(st));
        size_t r = wcrtomb(b, (wchar_t)cp, &st);
        if (r == (size_t)-1) return -1;
        if (n < r) return 0;
        memcpy(o, b, r);
        return (int)r;
    }
    case E_UTF16: case E_UTF16LE: case E_UTF16BE: {
        int le = c->to == E_UTF16LE, len = cp >= 0x10000 ? 4 : 2, bom = c->to == E_UTF16 && !c->bom_done;
        if (n < (size_t)(len + (bom ? 2 : 0))) return 0;
        if (bom) { o[0] = 0xFE; o[1] = 0xFF; o += 2; c->bom_done = 1; }
        unsigned u[2] = { (unsigned)cp, 0 };
        if (cp >= 0x10000) { u[0] = 0xD800 + ((cp - 0x10000) >> 10); u[1] = 0xDC00 + ((cp - 0x10000) & 0x3FF); }
        for (int i = 0; i < len / 2; i++) {
            o[2 * i] = le ? (unsigned char)u[i] : (unsigned char)(u[i] >> 8);
            o[2 * i + 1] = le ? (unsigned char)(u[i] >> 8) : (unsigned char)u[i];
        }
        return len + (bom ? 2 : 0);
    }
    default: {
        int le = c->to == E_UTF32LE, bom = c->to == E_UTF32 && !c->bom_done;
        if (n < (size_t)(4 + (bom ? 4 : 0))) return 0;
        if (bom) { o[0] = 0; o[1] = 0; o[2] = 0xFE; o[3] = 0xFF; o += 4; c->bom_done = 1; }
        for (int i = 0; i < 4; i++) o[i] = (unsigned char)(le ? cp >> (8 * i) : cp >> (8 * (3 - i)));
        return 4 + (bom ? 4 : 0);
    }
    }
}

size_t iconv(iconv_t cd, char** in, size_t* inleft, char** out, size_t* outleft) {
    ic_t* c = (ic_t*)cd;
    if (cd == (iconv_t)-1 || !c) { errno = EBADF; return (size_t)-1; }
    if (!in || !*in) {                                     /* back to the initial state */
        c->bom_done = 0;
        c->in_le = c->from == E_UTF16LE || c->from == E_UTF32LE;
        return 0;
    }
    size_t irreversible = 0;
    while (*inleft) {
        unsigned long cp;
        int k = ic_get(c, (const unsigned char*)*in, *inleft, &cp);
        if (k == -2) { int b = c->from == E_UTF16 ? 2 : 4; *in += b; *inleft -= b; continue; }   /* (a BOM) */
        if (k == 0) { errno = EINVAL; return (size_t)-1; }
        if (k < 0) {
            if (c->ignore) { (*in)++; (*inleft)--; irreversible++; continue; }
            errno = EILSEQ;
            return (size_t)-1;
        }
        int w = ic_put(c, cp, (unsigned char*)*out, *outleft);
        if (w < 0) {
            if (c->ignore) { *in += k; *inleft -= k; irreversible++; continue; }
            if (!c->translit) { errno = EILSEQ; return (size_t)-1; }
            w = ic_put(c, '?', (unsigned char*)*out, *outleft);
            if (w > 0) irreversible++;
        }
        if (w == 0) { errno = E2BIG; return (size_t)-1; }
        *in += k; *inleft -= k;
        *out += w; *outleft -= w;
    }
    return irreversible;
}
