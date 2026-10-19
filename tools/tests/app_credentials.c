/* Exercise the real app credential selection and task lookup without booting.
 * Unused app runtime sections are discarded by the host linker. */
#include "../../kernel/app.c"

extern int printf(const char*, ...);
static int current_pid;
int task_current_pid(void) { return current_pid; }

#define CHECK(expr) do { if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

int main(void) {
    app_credentials_t child;
    CHECK(APP_UID_ROOT == FS_UID_ROOT && APP_UID_BANANA == FS_UID_BANANA);
    current_pid = 0;
    CHECK(app_current_credentials()->euid == APP_UID_ROOT);
    CHECK(app_current_credentials()->egid == APP_UID_ROOT);
    credentials_for_launch(&child);
    CHECK(child.euid == APP_UID_BANANA && child.egid == APP_UID_BANANA);
    CHECK(child.group_count == 0);
    CHECK(app_credentials_in_group(&child, APP_UID_BANANA));
    CHECK(!app_credentials_in_group(&child, APP_UID_ROOT));

    /* Two distinct apps, with a non-default test identity and groups. */
    g_procs[0].used = 1;
    g_procs[0].credentials = child;
    g_procs[1].used = 1;
    g_procs[1].credentials = (app_credentials_t){
        .euid = 2000, .egid = 2001, .group_count = 2, .groups = {2002, 2003}
    };
    pid_set(10, 0, -1);
    pid_set(11, 1, -1);
    pid_set(12, 1, 0); /* worker thread resolves to its app, not root */
    current_pid = 10;
    CHECK(app_current_credentials()->euid == APP_UID_BANANA);
    current_pid = 11;
    const app_credentials_t* parent = app_current_credentials();
    CHECK(parent->euid == 2000 && parent->egid == 2001);
    CHECK(app_credentials_in_group(parent, 2001));
    CHECK(app_credentials_in_group(parent, 2002));
    CHECK(app_credentials_in_group(parent, 2003));
    CHECK(!app_credentials_in_group(parent, 0));
    CHECK(!app_credentials_in_group(parent, 2004));
    credentials_for_launch(&child);
    CHECK(child.euid == 2000 && child.egid == 2001);
    CHECK(child.group_count == 2 && child.groups[1] == 2003);
    current_pid = 12;
    CHECK(app_current_credentials() == parent);
    credentials_for_launch(&child);
    CHECK(child.euid == 2000 && child.groups[0] == 2002);
    g_procs[1].credentials.groups[0] = 9000;
    CHECK(child.groups[0] == 2002); /* child is a value snapshot */
    CHECK(g_procs[0].credentials.euid == APP_UID_BANANA);

    /* Every supported supplementary slot participates in membership. */
    app_credentials_t full = {.euid = 3000, .egid = 3000, .group_count = APP_GROUPS_MAX};
    for (uint32_t i = 0; i < APP_GROUPS_MAX; i++) full.groups[i] = 4000 + i;
    CHECK(app_credentials_in_group(&full, 4000 + APP_GROUPS_MAX - 1));
    CHECK(!app_credentials_in_group(&full, 4000 + APP_GROUPS_MAX));

    /* A cleared task mapping can be reused by a native service or an app. */
    pid_clear(12, 1);
    CHECK(app_current_credentials()->euid == APP_UID_ROOT);
    credentials_for_launch(&child);
    CHECK(child.euid == APP_UID_BANANA && child.group_count == 0);
    CHECK(child.groups[0] == 0 && child.groups[1] == 0);
    pid_set(12, 0, 0);
    CHECK(app_current_credentials()->euid == APP_UID_BANANA);
    pid_clear(12, 1); /* a different app must not clear the new mapping */
    CHECK(app_current_credentials()->euid == APP_UID_BANANA);
    pid_clear(12, 0);
    CHECK(app_current_credentials()->euid == APP_UID_ROOT);
    printf("PASS app credentials: native/user boundary, two identities, groups, child snapshots, threads, task reuse\n");
    return 0;
}
