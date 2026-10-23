/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "hostname.h"
#include "types.h"
#include "kstring.h"
#include "fs.h"
#include "config.h"

static char g_hostname[HOSTNAME_MAX + 1] = HOSTNAME_DEFAULT;
int hostname_valid(const char* s) {
    if (!s || !s[0]) return 0;
    unsigned n = 0;
    for (; s[n]; n++) {
        unsigned char c = (unsigned char)s[n];
        int alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (n >= HOSTNAME_MAX || (!alnum && c != '-') || (!n && c == '-')) return 0;
    }
    return s[n - 1] != '-';
}
static void normalize(char* out, const char* s) {
    unsigned i = 0;
    for (; s[i]; i++) out[i] = s[i] >= 'A' && s[i] <= 'Z' ? s[i] + ('a' - 'A') : s[i];
    out[i] = 0;
}
const char* hostname_get(void) { return g_hostname; }
void hostname_boot(void) {
    kstrlcpy(g_hostname, HOSTNAME_DEFAULT, sizeof(g_hostname));
    int idx = fs_find_file(HOSTNAME_PATH);
    if (idx < 0) return;
    char name[HOSTNAME_MAX + 3];
    int n = fs_read(idx, 0, name, sizeof(name) - 1);
    if (n <= 0) return;
    name[n] = 0;
    while (n && (name[n - 1] == '\n' || name[n - 1] == '\r')) name[--n] = 0;
    if ((int)strlen(name) == n && hostname_valid(name)) normalize(g_hostname, name);
}
int hostname_set(const char* name) {
    if (!hostname_valid(name)) return -1;
    char value[HOSTNAME_MAX + 2];
    normalize(value, name);
    unsigned n = (unsigned)strlen(value);
    value[n] = '\n';
    if (fs_write_path(HOSTNAME_PATH, value, n + 1u) < 0) return -2;
    value[n] = 0;
    kstrlcpy(g_hostname, value, sizeof(g_hostname));
    cfg_persist();
    return 0;
}
