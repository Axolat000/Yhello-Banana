/* SPDX-License-Identifier: BSD-2-Clause */
/* shelltest - checks GNU bash and zsh on Banana OS.
 *  - scripts (run.sh, run by bash): variables, arithmetic, control flow,
 *    redirections, here-documents, process substitution, functions, traps,
 *    jobs, read / printf, globs, the GNU tools in pipelines, zsh's own
 *    syntax - their output must be what the same scripts print on Linux
 *    (tests/gen.sh made it);
 *  - interactive (interactive.c): bash -i and zsh -i on a pseudo-terminal,
 *    typed at: line editing, history, Tab completion, ^C, ^Z, less...
 *   shelltest            both
 *   shelltest -s [name]  the scripts (those whose names have name)
 *   shelltest -i         the interactive checks
 *   shelltest -e         the editors that are installed (nano, vim, vi, nvim), typed at
 *   shelltest -k SHELL KEYS [MS]   keys typed at bash / zsh: what it shows
 *   shelltest -b N       the first N steps of the bash session, then ^D */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int shelltest_interactive(int verbose);
int shelltest_editors(void);
int shelltest_keys(const char* shell, const char* keys, int ms);
int shelltest_bash_steps(int n);

static int scripts(int argc, char** argv) {
    char* args[64];
    int n = 0;
    args[n++] = "bash";
    args[n++] = "/apps/shelltest/run.sh";
    for (int i = 0; i < argc && n < 63; i++) args[n++] = argv[i];
    args[n] = NULL;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        execv("/bin/bash", args);
        printf("shelltest: no /bin/bash (GNU bash comes with the 64-bit Banana OS)\n");
        _exit(127);
    }
    int st = 0;
    if (pid < 0 || waitpid(pid, &st, 0) != pid) return 1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

int main(int argc, char** argv) {
    if (access("/bin/bash", X_OK) != 0) {
        printf("shelltest: no /bin/bash (GNU bash comes with the 64-bit Banana OS)\n");
        return 1;
    }
    int failed = 0;
    if (argc > 3 && !strcmp(argv[1], "-k")) return shelltest_keys(argv[2], argv[3], argc > 4 ? atoi(argv[4]) : 3000);
    if (argc > 2 && !strcmp(argv[1], "-b")) return shelltest_bash_steps(atoi(argv[2]));
    if (argc > 1 && !strcmp(argv[1], "-i")) failed = shelltest_interactive(0);
    else if (argc > 1 && !strcmp(argv[1], "-e")) failed = shelltest_editors();
    else if (argc > 1 && !strcmp(argv[1], "-s")) failed = scripts(argc - 2, argv + 2);
    else {
        failed = scripts(0, NULL);
        failed += shelltest_interactive(0);
    }
    printf("shelltest: %s (%d failed)\n", failed ? "FAILED" : "all passed", failed);
    return failed;
}
