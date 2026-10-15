#ifndef SELFTEST_H
#define SELFTEST_H

/* `selftest [ring3 | memory | limits | cores]`: checks the system features
 * from inside Banana OS (shell/selftest.c) */
int selftest_cmd(int argc, char** argv);

#endif
