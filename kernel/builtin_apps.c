#include "builtin_apps.h"
#include "fs.h"
#include "kheap.h"
#include "kstring.h"
#include "pkg.h"
#include "serial.h"
#include "examples.h"
#include "repo.h"

extern const uint8_t app_mediaplayer[], app_mediaplayer_end[], app_music[], app_music_end[], app_amethyst[], app_amethyst_end[], app_photos[], app_photos_end[], app_code[], app_code_end[], app_store[], app_store_end[];

/* (stb_image's zlib inflater, third_party/stb) */
char* stbi_zlib_decode_malloc(const char* buffer, int len, int* outlen);

static const struct { const char* name; const uint8_t* start; const uint8_t* end; } APPS[] = {
    { "mediaplayer", app_mediaplayer, app_mediaplayer_end },
    { "music", app_music, app_music_end },
    { "amethyst", app_amethyst, app_amethyst_end },
    { "photos", app_photos, app_photos_end },
    { "code", app_code, app_code_end },
    { "store", app_store, app_store_end },
};

/* /etc/builtin-apps: "name=version" of each one installed from here */
#define MARKS "/etc/builtin-apps"

static int marked(const char* name, const char* version) {
    int fi = fs_find_file(MARKS);
    if (fi < 0) return 0;
    fs_file_t* f = fs_get_file(fi);
    if (!f || !f->content) return 0;
    char want[64];
    ksnprintf(want, sizeof(want), "%s=%s\n", name, version);
    return strstr(f->content, want) != NULL;
}

static void mark(const char* name, const char* version) {
    char line[64];
    ksnprintf(line, sizeof(line), "%s=%s\n", name, version);
    int fi = fs_find_file(MARKS);
    if (fi < 0) { fs_mkdir_p("/etc"); fi = fs_create(MARKS); }
    if (fi >= 0) fs_append(fi, line, (uint32_t)strlen(line));
}

/* /etc/builtin-apps-skip: "name\n" of each app an install profile left out */
#define SKIPS "/etc/builtin-apps-skip"

static int skipped(const char* name) {
    int fi = fs_find_file(SKIPS);
    fs_file_t* f = fi >= 0 ? fs_get_file(fi) : NULL;
    if (!f || !f->content) return 0;
    char want[40];
    ksnprintf(want, sizeof(want), "%s\n", name);
    for (const char* p = f->content; (p = strstr(p, want)); p++)
        if (p == f->content || p[-1] == '\n') return 1;
    return 0;
}

void builtin_apps_install(void) {
    for (unsigned i = 0; i < sizeof(APPS) / sizeof(APPS[0]); i++) {
        int zlen = (int)(APPS[i].end - APPS[i].start), len = 0;
        if (zlen <= 0) continue;
        uint8_t* bpk = (uint8_t*)stbi_zlib_decode_malloc((const char*)APPS[i].start, zlen, &len);
        if (!bpk || len <= 0) { klog("apps: %s: the built-in package is damaged\n", APPS[i].name); continue; }
        pkg_info_t mine, have;
        char err[96];
        if (pkg_inspect_mem(bpk, (uint32_t)len, &mine, err, sizeof(err)) == 0) {
            int installed = pkg_get(mine.name, &have) == 0;
            /* installed already (this version), removed by the user, or left out by
             * the install profile (and not installed again since): left as it is */
            if (!(installed && !strcmp(have.version, mine.version)) && !marked(mine.name, mine.version) &&
                !(skipped(mine.name) && !installed)) {
                char msg[160];
                if (pkg_install_mem(bpk, (uint32_t)len, msg, sizeof(msg)) == 0) {
                    mark(mine.name, mine.version);
                    klog("apps: %s\n", msg);
                } else klog("apps: %s: %s\n", mine.name, msg);
            }
        }
        kfree(bpk);
    }
    repo_builtin_refresh();          /* the App Store can install them again (builtin: source) */
}

/* ── the App Store's builtin: source ─────────────────────────────────── */

#define NAPPS ((int)(sizeof(APPS) / sizeof(APPS[0])))

int builtin_package_count(void) { return NAPPS + examples_count(); }

const char* builtin_package_file(int i) {
    static char name[40];
    if (i < 0) return "";
    if (i < NAPPS) { ksnprintf(name, sizeof(name), "%s.bpk", APPS[i].name); return name; }
    return examples_file(i - NAPPS);
}

int builtin_package(const char* file, char** data, uint32_t* len) {
    for (int i = 0; i < NAPPS; i++) {
        char name[40];
        ksnprintf(name, sizeof(name), "%s.bpk", APPS[i].name);
        if (strcmp(name, file)) continue;
        int zlen = (int)(APPS[i].end - APPS[i].start), n = 0;
        char* d = zlen > 0 ? stbi_zlib_decode_malloc((const char*)APPS[i].start, zlen, &n) : NULL;
        if (!d || n <= 0) { if (d) kfree(d); return -1; }
        *data = d;
        *len = (uint32_t)n;
        return 0;
    }
    for (int i = 0; i < examples_count(); i++) {
        if (strcmp(examples_file(i), file)) continue;
        uint32_t n = 0;
        const uint8_t* src = examples_data(i, &n);
        char* d = src && n ? (char*)kmalloc(n) : NULL;
        if (!d) return -1;
        memcpy(d, src, n);
        *data = d;
        *len = n;
        return 0;
    }
    return -1;
}

/* ── install profiles ────────────────────────────────────────────────── */

static const char* const PROFILE_NAMES[PROFILE_COUNT] = { "Full", "Standard", "Minimal" };
static const char* const PROFILE_APPS[PROFILE_COUNT] = {
    "every app: App Store, Media Player, Music, Photos, Amethyst Music, Banana Code, and the examples",
    "the everyday apps: App Store, Media Player, Music, Photos",
    "the App Store only - the other apps can be installed from it",
};
/* what each profile leaves out (package names; "" ends the list) */
static const char* const PROFILE_OUT[PROFILE_COUNT][6] = {
    { "" },
    { "amethyst", "code", "" },
    { "mediaplayer", "music", "photos", "amethyst", "code", "" },
};

const char* builtin_profile_name(int p) { return p >= 0 && p < PROFILE_COUNT ? PROFILE_NAMES[p] : ""; }
const char* builtin_profile_apps(int p) { return p >= 0 && p < PROFILE_COUNT ? PROFILE_APPS[p] : ""; }

int builtin_profile_apply(int p) {
    if (p <= PROFILE_FULL || p >= PROFILE_COUNT) return 0;
    int removed = 0;
    for (int k = 0; PROFILE_OUT[p][k][0]; k++) {
        const char* name = PROFILE_OUT[p][k];
        pkg_info_t have;
        char msg[160];
        if (pkg_get(name, &have) == 0 && pkg_remove(name, msg, sizeof(msg)) == 0) removed++;
        if (!skipped(name)) {                          /* not put back at the next boots */
            char line[40];
            ksnprintf(line, sizeof(line), "%s\n", name);
            int fi = fs_find_file(SKIPS);
            if (fi < 0) { fs_mkdir_p("/etc"); fi = fs_create(SKIPS); }
            if (fi >= 0) fs_append(fi, line, (uint32_t)strlen(line));
        }
    }
    /* the examples: in the App Store (builtin:), not in the home folder */
    if (fs_find_dir("/home/banana/Examples") >= 0) fs_delete("/home/banana/Examples", 1);
    char msg[96];
    repo_remove_source("file:///home/banana/Examples", msg, sizeof(msg));
    klog("install profile %s: %d apps left out (installable from the App Store)\n", PROFILE_NAMES[p], removed);
    return removed;
}

/* changes when this kernel's packages do (the App Store's list is made again) */
uint32_t builtin_packages_stamp(void) {
    uint32_t s = 2166136261u;
    for (int i = 0; i < NAPPS; i++) s = (s ^ (uint32_t)(APPS[i].end - APPS[i].start)) * 16777619u;
    for (int i = 0; i < examples_count(); i++) { uint32_t n = 0; examples_data(i, &n); s = (s ^ n) * 16777619u; }
    return s;
}
