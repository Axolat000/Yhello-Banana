/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "css.h"

static int failed,total;
static style_t* target(arena_t* a,const char* css,const char* html) {
    dom_node_t* doc=html_parse(a,html,(uint32_t)strlen(html));
    css_sheet_t* sheets[]={css_default_sheet(a),css_parse(a,css,(uint32_t)strlen(css),1)};
    css_style_tree(a,doc,sheets,2);
    dom_node_t* e=css_query(a,doc,"#target");
    return e?e->style:NULL;
}
static void test(const char* name,const char* css,const char* html,uint32_t color,int padding,int bold,int width) {
    arena_t a;arena_init(&a,8u<<20);style_t* s=target(&a,css,html);
    int ok=s && s->color==color && (padding<0||s->padding[0]==padding) && (bold<0||s->bold==bold) && (width==-999999||s->width==width);
    total++;if(!ok){failed++;printf("FAIL %s: color %06x padding %d bold %d width %d\n",name,s?s->color:0,s?s->padding[0]:-1,s?s->bold:0,s?s->width:0);}else printf("ok %s\n",name);
    arena_free_all(&a);
}
int main(void) {
    const char* h="<html><body><div id='target'></div></body></html>";
#define T(name,css,color) test(name,css,h,color,-1,-1,-999999)
    T("forward references","#target{--first:var(--last);--last:#123456;color:var(--first)}",0x123456);
    T("forward references across rules","#target{--first:var(--last);color:var(--first)} #target{--last:#123456}",0x123456);
    T("nested fallbacks","#target{color:var(--missing,var(--also-missing,rgb(4 5 6)))}",0x040506);
    T("mutual cycles","#target{--a:var(--b);--b:var(--a);color:var(--a,#123456)}",0x123456);
    T("self cycle with fallback is invalid","#target{--a:var(--a,red);color:var(--a,#123456)}",0x123456);
    T("cycles in unused fallback","#target{--ok:red;--a:var(--ok,var(--a));color:var(--a,#123456)}",0x123456);
    T("dependent fallback survives other cycle","#target{--a:var(--b);--b:var(--a);--c:var(--a,#123456);color:var(--c)}",0x123456);
    T("initial removes inherited value",":root{--a:red} #target{--a:initial;color:var(--a,#123456)}",0x123456);
    T("variable names are case sensitive","#target{--a:red;--A:#123456;color:var(--A)}",0x123456);
    T("important variable beats normal inline","#target{--a:#123456!important;color:var(--a)}",0x123456);
    test("important variable beats normal inline","#target{--a:#123456!important;color:var(--a)}","<html><body><div id='target' style='--a: red'></div></body></html>",0x123456,-1,-1,-999999);
    test("important inline variable beats important sheet","#target{--a:red!important;color:var(--a)}","<html><body><div id='target' style='--a: #123456!important'></div></body></html>",0x123456,-1,-1,-999999);
    test("important inline normal property wins","#target{color:red!important}","<html><body><div id='target' style='color:#123456!important'></div></body></html>",0x123456,-1,-1,-999999);
    test("inherited aliases are already computed",":root{--a:var(--b);--b:#123456} #target{--b:red;color:var(--a)}",h,0x123456,-1,-1,-999999);
    test("inherit keyword uses parent",":root{--a:#123456} #target{--a:inherit;color:var(--a)}",h,0x123456,-1,-1,-999999);
    test("variables in padding and numeric font weight","#target{--pad:1.5rem;--weight:700;padding:var(--pad);font-weight:var(--weight)}",h,0,24,1,-999999);
    test("empty values remain empty","#target{--empty:;color:red;color:var(--empty,blue)}",h,0,-1,-1,-999999);
    test("invalid variable uses inherited color","body{color:#123456} #target{color:red;color:var(--missing)}",h,0x123456,-1,-1,-999999);
    test("missing variable resets width","#target{width:12px;width:var(--missing)}",h,0,-1,-1,LEN_AUTO);
    test("quoted function text isn't replaced","#target{--a:\"var(--a)\";color:var(--missing,#123456)}",h,0x123456,-1,-1,-999999);
    printf("variables: %d passed, %d failed\n",total-failed,failed);return failed?1:0;
}
