/* Host regression test using the production DOM, selector and style APIs:
 * cc -std=c99 -Wall -Wextra -Iweb/test/shim -Iweb \
 *    web/arena.c web/html.c web/css.c web/test/selectors_test.c \
 *    -o /tmp/selectors_test && /tmp/selectors_test
 */
#include <stdio.h>
#include <string.h>
#include "css.h"

static int checks, failures;

static void check(int ok, const char* name, const char* operation) {
    checks++;
    if (!ok) {
        fprintf(stderr, "FAIL: %s (%s)\n", name, operation);
        failures++;
    }
}

static dom_node_t* fixture(arena_t* a) {
    const char* html = "<html><body><section class='group'>"
                       "<div id='target' class='a b c'></div>"
                       "<div id='other' class='decoy'></div>"
                       "</section></body></html>";
    return html_parse(a, html, (uint32_t)strlen(html));
}

static void utility(const char* name, const char* cls, const char* selector) {
    arena_t a;
    arena_init(&a, 0);
    dom_node_t* doc = fixture(&a);
    dom_node_t* target = dom_find_id(doc, "target");
    dom_node_t* other = dom_find_id(doc, "other");
    dom_set_attr(&a, target, "class", cls);
    check(css_matches(&a, target, selector), name, "matches");
    check(!css_matches(&a, other, selector), name, "nonmatch");
    check(css_query(&a, doc, selector) == target, name, "query");
    dom_node_t* found[4];
    int n = css_query_all(&a, doc, selector, found, 4);
    check(n == 1 && found[0] == target, name, "query all");

    char css[2048];
    snprintf(css, sizeof(css), "%s { color: #123456 } .decoy { color: #654321 }", selector);
    css_sheet_t* sheet = css_parse(&a, css, (uint32_t)strlen(css), 1);
    css_style_tree(&a, doc, &sheet, 1);
    check(target->style && target->style->color == 0x123456, name, "stylesheet/index");
    check(other->style && other->style->color == 0x654321, name, "following rule");
    check(!a.oom, name, "arena");
    arena_free_all(&a);
}

static void matching(const char* name, const char* selector, int expected) {
    arena_t a;
    arena_init(&a, 0);
    dom_node_t* doc = fixture(&a);
    dom_node_t* target = dom_find_id(doc, "target");
    dom_set_attr(&a, target, "data-token", "a\"),b");
    check(css_matches(&a, target, selector) == expected, name, "matches");
    dom_node_t* found[16];
    int n = css_query_all(&a, doc, selector, found, 16), has_target = 0;
    for (int i = 0; i < n; i++) if (found[i] == target) has_target = 1;
    check(has_target == expected, name, "query all");
    arena_free_all(&a);
}

static void color(const char* name, const char* css, uint32_t expected) {
    arena_t a;
    arena_init(&a, 0);
    dom_node_t* doc = fixture(&a);
    dom_node_t* target = dom_find_id(doc, "target");
    css_sheet_t* sheet = css_parse(&a, css, (uint32_t)strlen(css), 1);
    css_style_tree(&a, doc, &sheet, 1);
    check(target->style && target->style->color == expected, name, "computed color");
    arena_free_all(&a);
}

static void many_utilities(int count, int all_match) {
    arena_t a;
    arena_init(&a, 0);
    dom_node_t* doc = fixture(&a);
    dom_node_t* target = dom_find_id(doc, "target");
    dom_node_t* other = dom_find_id(doc, "other");
    size_t cls_cap = (size_t)count * 16 + 256, css_cap = (size_t)count * 100 + 2048;
    char* classes = (char*)arena_alloc(&a, (uint32_t)cls_cap);
    char* css = (char*)arena_alloc(&a, (uint32_t)css_cap);
    size_t cl = 0, sl = 0;
    for (int i = 0; i < count; i++) {
        cl += (size_t)snprintf(classes + cl, cls_cap - cl, "u-%d%s", i, i % 3 ? " " : "\t\n");
        if (all_match)
            sl += (size_t)snprintf(css + sl, css_cap - sl, ".u-%d{width:%dpx}", i, i + 1);
        else
            sl += (size_t)snprintf(css + sl, css_cap - sl,
                                  ".u-%d[data-active='%d']{color:#%06x}", i, i, 0x100000 + i);
    }
    char long_class[121];
    memset(long_class, 'x', sizeof(long_class) - 1);
    long_class[sizeof(long_class) - 1] = 0;
    snprintf(classes + cl, cls_cap - cl, "%s u-0 \t\n", long_class);
    /* Long names are indexed via any and tag respectively. Both buckets
     * must still be visited after all of the ordinary utility buckets. */
    sl += (size_t)snprintf(css + sl, css_cap - sl,
                          "#target{padding-top:7px}div{margin-top:9px}"
                          ".%s{height:11px}div.%s{background-color:#123456}", long_class, long_class);
    dom_set_attr(&a, target, "class", classes);
    css_sheet_t* sheet = css_parse(&a, css, (uint32_t)sl, 1);
    int rounds = all_match ? 1 : count;
    for (int i = 0; i < rounds; i++) {
        char active[16], name[80];
        snprintf(active, sizeof(active), "%d", i);
        snprintf(name, sizeof(name), "%d utilities, active %d", count, i);
        dom_set_attr(&a, target, "data-active", active);
        css_style_tree(&a, doc, &sheet, 1);
        check(target->style && (all_match ? target->style->width == count :
                               target->style->color == (uint32_t)(0x100000 + i)),
              name, all_match ? "all matches retained past initial capacity" : "every class bucket collected");
        check(target->style->padding[0] == 7, name, "ID bucket");
        check(target->style->margin[0] == 9, name, "tag bucket after classes");
        check(target->style->height == 11, name, "long class in any bucket");
        check(target->style->bg == 0x123456, name, "long class in tag bucket");
        check(other->style && other->style->height == LEN_AUTO && other->style->bg != 0x123456,
              name, "long class does not match unrelated element");
    }
    check(!a.oom, "many utilities", "arena");
    arena_free_all(&a);
}

int main(void) {
    static const struct { const char* name; const char* cls; const char* selector; } utilities[] = {
        { "hex digit and colon", "2xl:flex", ".\\32 xl\\:flex" },
        { "hex without whitespace terminator", "2xl:flex", ".\\32xl\\:flex" },
        { "six hex digits", "2xl:flex", ".\\000032xl\\:flex" },
        { "hex stops after six digits", "20xl:flex", ".\\0000320xl\\:flex" },
        { "six digits with terminator", "2xl:flex", ".\\000032 xl\\:flex" },
        { "tab terminator", "2xl:flex", ".\\32\txl\\:flex" },
        { "newline terminator", "2xl:flex", ".\\32\nxl\\:flex" },
        { "CRLF terminator", "2xl:flex", ".\\32\r\nxl\\:flex" },
        { "form feed terminator", "2xl:flex", ".\\32\fxl\\:flex" },
        { "hex punctuation", "md:flex", ".md\\3a flex" },
        { "hex escaped comma", "a,b", ".a\\2c b" },
        { "case preserved", "A", ".\\41" },
        { "UTF-8 two bytes", "\xc3\xa9", ".\\e9" },
        { "UTF-8 three bytes", "\xe2\x82\xac", ".\\20ac" },
        { "UTF-8 four bytes", "\xf0\x9f\x8d\x8c", ".\\1f34c" },
        { "null replacement", "\xef\xbf\xbd", ".\\0" },
        { "surrogate replacement", "\xef\xbf\xbd", ".\\d800" },
        { "out of range replacement", "\xef\xbf\xbd", ".\\110000" },
        { "consecutive expanding escapes", "\xef\xbf\xbd\xef\xbf\xbd", ".\\0\\0" },
        { "fraction", "w-1/2", ".w-1\\/2" },
        { "important utility", "!block", ".\\!block" },
        { "arbitrary value", "w-[calc(100%-1rem)]", ".w-\\[calc\\(100\\%-1rem\\)\\]" },
        { "arbitrary color", "bg-[#ff0000]", ".bg-\\[\\#ff0000\\]" },
        { "arbitrary selector variant", "[&>p]:block", ".\\[\\&\\>p\\]\\:block" },
        { "comma in class", "comma,utility", ".comma\\,utility" },
        { "comma in functional list", "comma,utility", ":is(.missing,.comma\\,utility)" },
        { "escaped closing parenthesis", "x)y", ":is(.x\\)y,.missing)" },
        { "escaped opening parenthesis", "x(y", ":is(.x\\(y,.missing)" },
        { "escaped bracket and comma", "x],y", ".x\\]\\,y" },
        { "literal backslash", "x\\y", ".x\\\\y" },
        { "escaped quote in class", "x'y", ".x\\'y" },
        { "escaped semicolon", "x;y", ".x\\;y" },
        { "escaped braces", "x{y}", ".x\\{y\\}" },
        { "hex followed by descendant whitespace", "a", ".grou\\70  .a" },
        { "hex within child selector", "2xl:flex", "section > .\\32 xl\\:flex" },
        { "long arbitrary utility", "bg-[linear-gradient(to_right,red_0%,orange_20%,yellow_40%,green_60%,blue_80%,violet_100%)]-extra-long-name",
          ".bg-\\[linear-gradient\\(to_right\\,red_0\\%\\,orange_20\\%\\,yellow_40\\%\\,green_60\\%\\,blue_80\\%\\,violet_100\\%\\)\\]-extra-long-name" },
    };
    for (unsigned i = 0; i < sizeof(utilities) / sizeof(utilities[0]); i++)
        utility(utilities[i].name, utilities[i].cls, utilities[i].selector);

    matching("repeated is true", ":is(.a,.missing):is(.b,.missing)", 1);
    matching("repeated is first fails", ":is(.missing):is(.b)", 0);
    matching("repeated is last fails", ":is(.a):is(.missing)", 0);
    matching("repeated not true", ":not(.missing):not(.absent)", 1);
    matching("repeated not first excludes", ":not(.a):not(.missing)", 0);
    matching("repeated not last excludes", ":not(.missing):not(.b)", 0);
    matching("mixed is and where true", ":is(.a):where(.b)", 1);
    matching("mixed is and where false", ":is(.missing):where(.b)", 0);
    matching("repeated where false", ":where(.missing):where(.b)", 0);
    matching("nested functions", ":is(:not(.missing),.absent):not(:is(.missing,.absent))", 1);
    matching("complex argument", ":is(section > .a):not(body > .a)", 1);
    matching("repeated nth true", ":nth-child(odd):nth-child(1)", 1);
    matching("repeated nth first fails", ":nth-child(even):nth-child(1)", 0);
    matching("different nth conditions", ":nth-child(2):nth-last-child(2)", 0);
    matching("escaped ID", "#\\74 arget", 1);
    matching("escaped tag", "\\64 iv.a", 1);
    matching("escaped pseudo name", ":\\69 s(.a)", 1);
    matching("escaped attribute name", "[\\69 d=target]", 1);
    matching("escaped attribute value", "[id=\\74 arget]", 1);
    matching("quoted escaped string and delimiters", ":is([data-token=\"a\\\"),b\"],.missing)", 1);
    matching("quoted string with hex quote", ":is([data-token=\"a\\22 ),b\"],.missing)", 1);
    matching("escaped comma does not split", ".missing\\,a", 0);
    matching("escaped quote does not hide list comma", ".missing\\', .a", 1);
    matching("trailing escape rejected", ".a\\", 0);
    matching("escape newline rejected", ".a\\\n", 0);
    matching("unclosed function rejected", ":is(.a", 0);
    matching("empty not rejected", ":not()", 0);

    color("is uses max even for unmatched ID", ":is(.a,#absent){color:#112233}.a.b{color:#445566}", 0x112233);
    color("not uses max argument", ":not(#absent,.absent){color:#112233}.a.b{color:#445566}", 0x112233);
    color("is type specificity", ".a{color:#112233}:is(div){color:#445566}", 0x112233);
    color("not type specificity", ".a{color:#112233}:not(span){color:#445566}", 0x112233);
    color("is adds no extra pseudo weight", ".a{color:#112233}:is(.b){color:#445566}", 0x445566);
    color("not adds no extra pseudo weight", ":not(.absent){color:#112233}.a{color:#445566}", 0x445566);
    color("is max rather than sum", ":is(.a,.b,.c){color:#112233}.a.b{color:#445566}", 0x445566);
    color("where zero including ID", ".a{color:#112233}:where(#target,.a){color:#445566}", 0x112233);
    color("where zero for complex arguments", "div{color:#112233}:where(section > #target.a){color:#445566}", 0x112233);
    color("where preserves outer specificity", "div{color:#112233}.a:where(#target){color:#445566}", 0x445566);
    color("nested where zero", ".a{color:#112233}:is(:where(#target)){color:#445566}", 0x112233);
    color("nested max specificity", ":is(:not(#absent),.absent){color:#112233}.a.b{color:#445566}", 0x112233);
    color("complex argument specificity", ":is(section > #target){color:#112233}#target{color:#445566}", 0x112233);
    color("repeated functions add specificity", ":is(.a):is(.b){color:#112233}.a{color:#445566}", 0x112233);
    color("repeated functions must all match", ".a{color:#112233}:is(.missing):is(#target){color:#445566}", 0x112233);
    color("repeated not must all match", ".a{color:#112233}:not(#target):not(#absent){color:#445566}", 0x112233);
    color("quoted attribute delimiters in stylesheet", ":is([id=\"target\"], [id=\"x),y\"]){color:#112233}", 0x112233);
    color("escaped string quote in stylesheet", ":is([id=\"x\\\"),y\"], #target){color:#112233}", 0x112233);
    color("deduplication across class and any buckets", ".a,:is(#target){color:#112233!important}.b.c{color:#445566!important}", 0x112233);
    color("deduplication across ID and class buckets", "#target,.a{color:#112233!important}.b.c{color:#445566!important}", 0x112233);
    many_utilities(40, 0);
    many_utilities(2057, 1);

    printf("selectors: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
