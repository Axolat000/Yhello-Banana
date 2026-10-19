/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "framework_fixtures.h"
#include "kstring.h"
#define DECLARE(name) extern const unsigned char framework_##name[], framework_##name##_end[]
DECLARE(react);DECLARE(reactdom);DECLARE(vue);DECLARE(tailwind);DECLARE(reacthtml);DECLARE(vuehtml);
DECLARE(alpine);DECLARE(alpinehtml);
DECLARE(licenses);
DECLARE(mutations);DECLARE(mutations_async);DECLARE(mutations_limits);
#define ASTRO_RESOURCE(url,name,type) DECLARE(name);
#include "fixtures/astro_resources.inc"
#undef ASTRO_RESOURCE
int framework_fixture(const char* url,const char** text,uint32_t* len,const char** type) {
#define RESOURCE(path,name,ctype) if(!strcmp(url,path)) { *text=(const char*)framework_##name; *len=(uint32_t)(framework_##name##_end-framework_##name); *type=ctype; return 1; }
    RESOURCE("about:frameworks",reacthtml,"text/html");
    RESOURCE("about:frameworks/",reacthtml,"text/html");
    RESOURCE("about:frameworks/vue",vuehtml,"text/html");
    RESOURCE("about:frameworks/alpine",alpinehtml,"text/html");
    RESOURCE("about:frameworks/licenses",licenses,"text/html");
    RESOURCE("about:frameworks/react.js",react,"application/javascript");
    RESOURCE("about:frameworks/react-dom.js",reactdom,"application/javascript");
    RESOURCE("about:frameworks/vue.js",vue,"application/javascript");
    RESOURCE("about:frameworks/alpine.js",alpine,"application/javascript");
    RESOURCE("about:frameworks/tailwind.css",tailwind,"text/css");
    RESOURCE("about:frameworks/tests/mutations",mutations,"text/html");
    RESOURCE("about:frameworks/tests/mutations-async",mutations_async,"text/html");
    RESOURCE("about:frameworks/tests/mutations-limits",mutations_limits,"text/html");
#define ASTRO_RESOURCE(url,name,type) RESOURCE(url,name,type);
#include "fixtures/astro_resources.inc"
#undef ASTRO_RESOURCE
#undef RESOURCE
    return 0;
}
