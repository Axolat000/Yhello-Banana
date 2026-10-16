#ifndef FSTEST_H
#define FSTEST_H

/* `fstest [-k] <mount point>`: writes to a FAT32 / exFAT drive, mounts it
 * again and checks it all (shell/fstest.c) */
int fstest_cmd(int argc, char** argv);

#endif
