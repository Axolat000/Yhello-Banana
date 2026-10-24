/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../web/page.h"
#include "../../kernel/kstring.h"
#include <stdlib.h>
#include <string.h>

#define READER_LIMIT (2u << 20)
typedef struct {
    char* text;
    uint32_t length, capacity;
    int failed;
} buffer_t;

static void append(buffer_t* b, const char* text) {
    uint32_t n = (uint32_t)strlen(text);
    if (n > READER_LIMIT - b->length) { b->failed = 1; return; }
    if (b->length + n + 1 > b->capacity) {
        uint32_t cap = (b->length + n + 1) * 2;
        if (cap > READER_LIMIT + 1) cap = READER_LIMIT + 1;
        char* next = realloc(b->text, cap);
        if (!next) { b->failed = 1; return; }
        b->text = next;
        b->capacity = cap;
    }
    memcpy(b->text + b->length, text, n + 1);
    b->length += n;
}

static void escape(buffer_t* b, const char* text) {
    for (; text && *text; text++) {
        char single[2] = { *text, 0 };
        append(b, *text == '&' ? "&amp;" : *text == '<' ? "&lt;" :
                  *text == '>' ? "&gt;" : *text == '"' ? "&quot;" : single);
        if (b->failed) return;
    }
}

static dom_node_t* class_node(dom_node_t* root, const char* name, int depth) {
    if (!root || depth > 128) return NULL;
    if (dom_has_class(root, name)) return root;
    for (dom_node_t* n = root->first; n; n = n->next) {
        dom_node_t* found = class_node(n, name, depth + 1);
        if (found) return found;
    }
    return NULL;
}

static dom_node_t* fragment_node(dom_node_t* root, int depth) {
    if (!root || depth > 128) return NULL;
    if (root->tag && !strcmp(root->tag, "include-fragment")) {
        const char* src=dom_attr(root,"src");
        if(src&&strstr(src,"/releases/expanded_assets/"))return root;
    }
    for (dom_node_t* n = root->first; n; n = n->next) {
        dom_node_t* found = fragment_node(n, depth + 1);
        if (found) return found;
    }
    return NULL;
}

static void assets(buffer_t* b, arena_t* arena, dom_node_t* root, const char* base, int depth) {
    if (!root || depth > 128) return;
    if (root->tag && !strcmp(root->tag, "a")) {
        const char* href = dom_attr(root, "href");
        if (href && (strstr(href, "/releases/download/") || strstr(href, "/archive/"))) {
            char url[8192];
            url_resolve(base, href, url, sizeof(url));
            if (!strncmp(url, "https://github.com/", 19)) {
                append(b, "<li><a href=\""); escape(b, url); append(b, "\">");
                escape(b, dom_text(arena, root)); append(b, "</a></li>");
            }
        }
    }
    for (dom_node_t* n = root->first; n; n = n->next) assets(b, arena, n, base, depth + 1);
}

typedef struct {
    page_env_t* env;
    arena_t* arena;
    buffer_t* buffer;
    const char* url;
    char seen[16][512];
    int count;
} releases_t;

static void release_nodes(releases_t* r, dom_node_t* root, int depth) {
    if (!root || depth > 128 || r->count >= 16 || r->buffer->failed) return;
    if (root->tag && !strcmp(root->tag, "a")) {
        const char* href = dom_attr(root, "href");
        if (href && strstr(href, "/releases/tag/") && strlen(href) < sizeof(r->seen[0])) {
            int seen = 0;
            for (int i = 0; i < r->count; i++) if (!strcmp(href, r->seen[i])) seen = 1;
            dom_node_t* box = root->parent;
            while (box && !dom_has_class(box, "Box")) box = box->parent;
            if (!seen && box && href[0] == '/' && href[1] != '/') {
                kstrlcpy(r->seen[r->count++], href, sizeof(r->seen[0]));
                char target[8192];
                url_resolve(r->url, href, target, sizeof(target));
                append(r->buffer, "<h2><a href=\""); escape(r->buffer, target);
                append(r->buffer, "\">"); escape(r->buffer, dom_text(r->arena, root));
                append(r->buffer, "</a></h2>");
                dom_node_t* notes = class_node(box, "markdown-body", 0);
                if (notes) {
                    append(r->buffer, "<pre>");
                    escape(r->buffer, dom_text(r->arena, notes));
                    append(r->buffer, "</pre>");
                }
                append(r->buffer, "<h3>Downloads</h3><ul>");
                assets(r->buffer, r->arena, box, r->url, 0);
                dom_node_t* fragment = fragment_node(box, 0);
                const char* src = fragment ? dom_attr(fragment, "src") : NULL;
                if (src && strstr(src, "/releases/expanded_assets/")) {
                    char resource[8192], final[8192], type[96], error[160];
                    char* data = NULL;
                    uint32_t size = 0;
                    url_resolve(r->url, src, resource, sizeof(resource));
                    if (!strncmp(resource, "https://github.com/", 19) && r->env->fetch &&
                        !r->env->fetch(r->env->ctx, resource, &data, &size, type, sizeof(type),
                                       final, sizeof(final), error, sizeof(error))) {
                        dom_node_t* parsed = html_parse(r->arena, data, size);
                        assets(r->buffer, r->arena, parsed, final, 0);
                    }
                    free(data);
                }
                append(r->buffer, "</ul><hr>");
            }
        }
    }
    for (dom_node_t* n = root->first; n; n = n->next) release_nodes(r, n, depth + 1);
}

/* GitHub release pages hydrate downloads through include-fragment elements.
 * Render their server-provided release notes and real download links in a
 * lightweight view, without requiring GitHub's complete application bundle.
 * All HTML and fragment parsing stays inside this restricted process. */
char* worker_github_releases(page_env_t* env, const char* url, const char* html,
                             uint32_t length, uint32_t* output_length) {
    if (strncmp(url, "https://github.com/", 19)) return NULL;
    const char* owner = url + 19;
    const char* slash = strchr(owner, '/');
    const char* releases = slash ? strchr(slash + 1, '/') : NULL;
    if (!releases || strncmp(releases, "/releases", 9) ||
        (releases[9] && releases[9] != '/' && releases[9] != '?' && releases[9] != '#') ||
        !strncmp(releases, "/releases/download/", 19)) return NULL;
    arena_t arena;
    arena_init(&arena, 16u << 20);
    dom_node_t* doc = html_parse(&arena, html, length);
    buffer_t b = {0};
    append(&b, "<html><head><title>GitHub Releases</title><style>"
               "body{font-family:sans-serif;padding:16px;background:#fff;color:#202830}"
               "a{color:#0969da}pre{white-space:pre-wrap;font-size:14px}"
               "h2{margin-top:24px}li{padding:5px}</style></head><body><h1>GitHub Releases</h1><p>");
    escape(&b, url);
    append(&b, "</p>");
    releases_t r = { .env = env, .arena = &arena, .buffer = &b, .url = url };
    release_nodes(&r, doc, 0);
    append(&b, "</body></html>");
    arena_free_all(&arena);
    if (!r.count || b.failed) { free(b.text); return NULL; }
    *output_length = b.length;
    return b.text;
}
