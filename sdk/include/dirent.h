#ifndef _DIRENT_H
#define _DIRENT_H
#ifdef __cplusplus
extern "C" {
#endif
/* reading folders */
#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8
#define DT_LNK     10

struct dirent {
    ino_t          d_ino;
    off_t          d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[256];
};
#define dirent64 dirent

typedef struct __dir DIR;

DIR*           opendir(const char* path);
DIR*           fdopendir(int fd);
struct dirent* readdir(DIR* d);
int            readdir_r(DIR* d, struct dirent* e, struct dirent** out);
int            closedir(DIR* d);
void           rewinddir(DIR* d);
int            dirfd(DIR* d);
#define readdir64 readdir
#ifdef __cplusplus
}
#endif
#endif
