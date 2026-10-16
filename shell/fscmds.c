#include "fscmds.h"
#include "netcmds.h"
#include "../kernel/terminal.h"
#include "../kernel/kstring.h"
#include "../kernel/kheap.h"
#include "../kernel/fs.h"
#include "../kernel/fsdisk.h"
#include "../kernel/rtc.h"

static void err(const char* cmd, const char* what, const char* path) {
    char b[FS_PATH_LEN + 64];
    ksnprintf(b, sizeof(b), "%s: %s%s%s", cmd, path ? path : "", path ? ": " : "", what);
    terminal_write_color(b, VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    terminal_putchar('\n');
}

/* why the last fs_* call failed, in the words ls / chmod use */
static const char* why(void) {
    if (fs_perm_error()) return "Permission denied";
    if (fs_io_error()) return "Operation not supported on this volume (FAT / exFAT keep no modes, owners or links)";
    return "No such file or directory";
}

/* "root", "banana" or a number: the uid, or -1 */
static int parse_user(const char* s) {
    if (strcmp(s, "root") == 0) return (int)FS_UID_ROOT;
    if (strcmp(s, "banana") == 0) return (int)FS_UID_BANANA;
    uint32_t v;
    if (k_parse_u32(s, &v) && v < 65536u) return (int)v;
    return -1;
}

static int octal(const char* s, uint32_t* out) {
    if (!*s) return 0;
    uint32_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '7') return 0;
        v = v * 8u + (uint32_t)(*s - '0');
        if (v > 07777u) return 0;
    }
    *out = v;
    return 1;
}

/* "u+x", "go-w", "a=r", "+x", "u=rwx,g=rx,o=" applied to cur */
static int symbolic(const char* s, uint32_t cur, int is_dir, uint32_t* out) {
    uint32_t m = cur & 07777u;
    while (*s) {
        uint32_t who = 0;
        for (; *s == 'u' || *s == 'g' || *s == 'o' || *s == 'a'; s++)
            who |= *s == 'u' ? 04700u : *s == 'g' ? 02070u : *s == 'o' ? 01007u : 07777u;
        if (!who) who = 07777u;
        char op = *s;
        if (op != '+' && op != '-' && op != '=') return 0;
        s++;
        uint32_t bits = 0;
        for (; *s && *s != ','; s++) {
            if (*s == 'r') bits |= 0444u;
            else if (*s == 'w') bits |= 0222u;
            else if (*s == 'x') bits |= 0111u;
            else if (*s == 'X') { if (is_dir || (cur & 0111u)) bits |= 0111u; }
            else if (*s == 's') bits |= 06000u;
            else if (*s == 't') bits |= 01000u;
            else return 0;
        }
        bits &= who;
        if (op == '+') m |= bits;
        else if (op == '-') m &= ~bits;
        else m = (m & ~(who & 0777u)) | bits;
        if (*s == ',') s++;
    }
    *out = m;
    return 1;
}

static void cmd_chmod(int argc, char** argv) {
    if (argc < 3) { terminal_writeln("usage: chmod MODE FILE...   (MODE: 755, 0644, u+x, go-w, a=r, u=rwx,g=rx,o=)"); return; }
    for (int i = 2; i < argc; i++) {
        fs_stat_t st;
        if (fs_stat(argv[i], 1, &st) != 0) { err("chmod", "No such file or directory", argv[i]); continue; }
        uint32_t m;
        if (!octal(argv[1], &m) && !symbolic(argv[1], st.mode, st.is_dir, &m)) {
            err("chmod", "invalid mode", argv[1]);
            return;
        }
        if (fs_chmod(argv[i], m) != 0) err("chmod", why(), argv[i]);
    }
}

static void cmd_chown(int argc, char** argv, int group_only) {
    const char* cmd = group_only ? "chgrp" : "chown";
    if (argc < 3) {
        terminal_writeln(group_only ? "usage: chgrp GROUP FILE...   (root, banana or a number)"
                                    : "usage: chown OWNER[:GROUP] FILE...   (root, banana or a number)");
        return;
    }
    int uid = -1, gid = -1;
    char spec[64];
    kstrlcpy(spec, argv[1], sizeof(spec));
    if (group_only) {
        gid = parse_user(spec);
        if (gid < 0) { err(cmd, "invalid group", spec); return; }
    } else {
        char* colon = spec;
        while (*colon && *colon != ':' && *colon != '.') colon++;
        if (*colon) {
            *colon = 0;
            if (colon[1] && (gid = parse_user(colon + 1)) < 0) { err(cmd, "invalid group", colon + 1); return; }
        }
        if (spec[0] && (uid = parse_user(spec)) < 0) { err(cmd, "invalid user", spec); return; }
    }
    for (int i = 2; i < argc; i++)
        if (fs_chown(argv[i], uid, gid) != 0) err(cmd, why(), argv[i]);
}

static void cmd_ln(int argc, char** argv) {
    int sym = 0, force = 0, i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (const char* o = argv[i] + 1; *o; o++) {
            if (*o == 's') sym = 1;
            else if (*o == 'f') force = 1;
            else { err("ln", "unknown option", argv[i]); return; }
        }
    }
    if (argc - i != 2) { terminal_writeln("usage: ln -s TARGET LINK_NAME   (a symbolic link; -f replaces LINK_NAME)"); return; }
    if (!sym) {
        err("ln", "hard links are not supported - use a symbolic link (ln -s)", NULL);
        return;
    }
    const char* target = argv[i];
    char link[FS_PATH_LEN];
    kstrlcpy(link, argv[i + 1], sizeof(link));
    /* ln -s target dir/ : the link goes into the folder, named like the target */
    if (fs_find_dir(link) >= 0) {
        const char* base = target;
        for (const char* p = target; *p; p++) if (*p == '/' && p[1]) base = p + 1;
        int l = (int)strlen(link);
        ksnprintf(link + l, sizeof(link) - (size_t)l, "%s%s", (l && link[l - 1] == '/') ? "" : "/", base);
    }
    fs_stat_t st;
    if (fs_stat(link, 0, &st) == 0) {
        if (!force || st.is_dir) { err("ln", "File exists", link); return; }
        fs_delete(link, 0);
    }
    if (fs_symlink(target, link) < 0) err("ln", why(), link);
}

static void cmd_readlink(int argc, char** argv) {
    if (argc < 2) { terminal_writeln("usage: readlink LINK"); return; }
    char t[FS_PATH_LEN];
    for (int i = 1; i < argc; i++) {
        if (fs_readlink(argv[i], t, sizeof(t)) < 0) err("readlink", "not a symbolic link", argv[i]);
        else terminal_writeln(t);
    }
}

static void when(uint32_t t, char* out, int cap) {
    if (!t) { kstrlcpy(out, "-", (size_t)cap); return; }
    rtc_datetime_t d;
    rtc_split_time(t, &d);
    ksnprintf(out, (size_t)cap, "%04u-%02u-%02u %02u:%02u:%02u", d.year, d.month, d.day, d.hour, d.minute, d.second);
}

static const char* who(uint32_t id, char* buf, int cap) {
    if (id == FS_UID_ROOT) return "root";
    if (id == FS_UID_BANANA) return "banana";
    ksnprintf(buf, (size_t)cap, "%u", id);
    return buf;
}

static void cmd_stat(int argc, char** argv) {
    int follow = 0, i = 1;
    if (i < argc && strcmp(argv[i], "-L") == 0) { follow = 1; i++; }
    if (i >= argc) { terminal_writeln("usage: stat [-L] FILE...   (-L: follow symbolic links)"); return; }
    for (; i < argc; i++) {
        fs_stat_t st;
        if (fs_stat(argv[i], follow, &st) != 0) { err("stat", "No such file or directory", argv[i]); continue; }
        char b[FS_PATH_LEN + 80], t[FS_PATH_LEN], u[12], g[12], perm[11];
        if (st.is_link && fs_readlink(argv[i], t, sizeof(t)) >= 0)
            ksnprintf(b, sizeof(b), "  File: %s -> %s", argv[i], t);
        else
            ksnprintf(b, sizeof(b), "  File: %s", argv[i]);
        terminal_writeln(b);
        const char* kind = st.is_dir ? "directory" : st.is_link ? "symbolic link" : st.size ? "regular file" : "regular empty file";
        ksnprintf(b, sizeof(b), "  Size: %-12u Blocks: %-8u IO Block: 4096   %s", st.size, (st.size + 511u) / 512u, kind);
        terminal_writeln(b);
        ksnprintf(b, sizeof(b), "Device: %s   Inode: %d", st.mnt ? fs_mount_point(st.mnt) : "/", st.index);
        terminal_writeln(b);
        uint32_t ty = st.mode & FS_S_IFMT;
        perm[0] = ty == FS_S_IFDIR ? 'd' : ty == FS_S_IFLNK ? 'l' : '-';
        for (int k = 0; k < 9; k++) perm[1 + k] = (st.mode & (0400u >> k)) ? "rwxrwxrwx"[k] : '-';
        perm[10] = 0;
        ksnprintf(b, sizeof(b), "Access: (%04o/%s)  Uid: (%5u/%7s)   Gid: (%5u/%7s)", st.mode & 07777u, perm,
                  st.uid, who(st.uid, u, sizeof(u)), st.gid, who(st.gid, g, sizeof(g)));
        terminal_writeln(b);
        when(st.atime, t, sizeof(t)); ksnprintf(b, sizeof(b), "Access: %s", t); terminal_writeln(b);
        when(st.mtime, t, sizeof(t)); ksnprintf(b, sizeof(b), "Modify: %s", t); terminal_writeln(b);
        when(st.ctime, t, sizeof(t)); ksnprintf(b, sizeof(b), " Birth: %s", t); terminal_writeln(b);
    }
}

static void say(const char* line) {
    terminal_write("  ");
    terminal_writeln(line);
}

static void cmd_fsck(int argc, char** argv) {
    int full = 0, fix = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') continue;
        for (const char* o = argv[i] + 1; *o; o++) {
            if (*o == 'f') full = 1;
            else if (*o == 'y' || *o == 'p' || *o == 'a') fix = 1;
            else if (*o == 'n') fix = 0;
            else { terminal_writeln("usage: fsck [-f] [-y|-n]   (-f: read every file back, -y: repair the tree)"); return; }
        }
    }
    terminal_writeln("fsck: Banana OS filesystem (layout 5, metadata version 7)");
    terminal_writeln("Pass 1: the tree in memory (folders, files, names, links)");
    int p1 = fs_check(fix, say);
    char b[96];
    ksnprintf(b, sizeof(b), "  %u folders, %u files - %d problem%s%s", fs_used_dirs(), fs_used_files(), p1, p1 == 1 ? "" : "s",
              p1 && fix ? " (repaired)" : "");
    terminal_writeln(b);
    terminal_writeln(full ? "Pass 2: the installed disk (metadata, extents, every file's checksum)"
                          : "Pass 2: the installed disk (metadata, extents; -f also reads every file)");
    int p2 = fsdisk_check(full, say);
    int total = p1 + p2;
    if (total == 0) terminal_write_color("fsck: clean\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    else {
        ksnprintf(b, sizeof(b), "fsck: %d problem%s found%s\n", total, total == 1 ? "" : "s",
                  fix ? "" : " - run fsck -y to repair the tree");
        terminal_write_color(b, VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    }
}

int fscmd_dispatch(const char* line) {
    char* buf = (char*)kmalloc(1024);
    if (!buf) return 0;
    kstrlcpy(buf, line, 1024);
    char* argv[32];
    int argc = shell_split_args(buf, argv, 32), done = 1;
    if (argc == 0) done = 0;
    else if (strcmp(argv[0], "chmod") == 0) cmd_chmod(argc, argv);
    else if (strcmp(argv[0], "chown") == 0) cmd_chown(argc, argv, 0);
    else if (strcmp(argv[0], "chgrp") == 0) cmd_chown(argc, argv, 1);
    else if (strcmp(argv[0], "ln") == 0) cmd_ln(argc, argv);
    else if (strcmp(argv[0], "readlink") == 0) cmd_readlink(argc, argv);
    else if (strcmp(argv[0], "stat") == 0) cmd_stat(argc, argv);
    else if (strcmp(argv[0], "fsck") == 0 || strcmp(argv[0], "e2fsck") == 0) cmd_fsck(argc, argv);
    else done = 0;
    kfree(buf);
    return done;
}
