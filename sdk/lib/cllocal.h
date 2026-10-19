/* SPDX-License-Identifier: BSD-2-Clause */
/* Source lowering for block-scope __local arrays. Parameter qualifiers are
 * handled by param(); arrays get one aligned allocation per work-group. */
static int lower_local_arrays(cl_program p, SB* out) {
    const char* src = p->src;
    const char* copied = src;
    unsigned slot = 0;
    for (const char* s = src; *s;) {
        if (s[0] == '/' && (s[1] == '/' || s[1] == '*')) { s = skip_ws(s); continue; }
        if (*s == '"' || *s == '\'') {
            char quote = *s++;
            while (*s && *s != quote) { if (*s == '\\' && s[1]) s++; s++; }
            if (*s) s++;
            continue;
        }
        int len = word_at(src, s, "__local") ? 7 : word_at(src, s, "local") ? 5 : 0;
        if (!len) { s++; continue; }
        const char* type = skip_ws(s + len);
        const char* q = type;
        while (*q && *q != '[' && *q != '*' && *q != '(' && *q != ')' && *q != ';' && *q != ',' && *q != '{' && *q != '}') q++;
        if (*q != '[') { s += len; continue; }
        const char* name_end = q;
        while (name_end > type && (name_end[-1] == ' ' || name_end[-1] == '\t' || name_end[-1] == '\n')) name_end--;
        const char* name = name_end;
        while (name > type && is_ident((unsigned char)name[-1])) name--;
        if (name == type || name == name_end || slot >= BCL_LOCAL_SLOTS) {
            log_add(p, "invalid __local array declaration (at most 32 declarations)"); return -1;
        }
        const char* dims = q;
        const char* rest_dims = NULL;
        int depth = 0;
        do {
            if (*q == '[') depth++;
            else if (*q == ']') depth--;
            q++;
            if (!depth) { q = skip_ws(q); if (!rest_dims) rest_dims = q; }
        } while (*q && (depth || *q == '['));
        if (*q != ';') { log_add(p, "__local arrays need a single declaration without an initializer"); return -1; }
        sb_put(out, copied, (size_t)(s - copied));
        char buf[64];
        sb_put(out, type, (size_t)(name - type));
        if (rest_dims < q) sb_str(out, " (*"); else sb_str(out, " *");
        sb_put(out, name, (size_t)(name_end - name));
        if (rest_dims < q) { sb_str(out, ")"); sb_put(out, rest_dims, (size_t)(q - rest_dims)); }
        sb_str(out, " = ("); sb_put(out, type, (size_t)(name - type));
        if (rest_dims < q) { sb_str(out, " (*)"); sb_put(out, rest_dims, (size_t)(q - rest_dims)); }
        else sb_str(out, " *");
        snprintf(buf, sizeof(buf), ")__bcl_local(__bcl_current(), %u, sizeof(", slot++);
        sb_str(out, buf); sb_put(out, type, (size_t)(name - type));
        sb_put(out, dims, (size_t)(q - dims)); sb_str(out, "));\n");
        s = copied = q + 1;
    }
    sb_put(out, copied, strlen(copied));
    return out->oom ? -1 : 0;
}
