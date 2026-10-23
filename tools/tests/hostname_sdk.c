/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../sdk/include/banana_api.h"
#include <unistd.h>
#include <sys/utsname.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL SDK line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static const char* file;
static int fail_read;
static int opened(const char* p,int flags) { CHECK(!strcmp(p,"/etc/hostname") && flags==BANANA_O_READ); return file ? 3 : -1; }
static long read_file(int fd,void* out,unsigned long n) {
    CHECK(fd==3); if (fail_read) return -1;
    size_t len=strlen(file); if (len>n) len=n; memcpy(out,file,len); return (long)len;
}
static int closed(int fd) { CHECK(fd==3); return 0; }
static const banana_api_t api={.arch="x86_64",.os_version="0.6",.open=opened,.read=read_file,.close=closed};
const banana_api_t* __banana=&api;
int main(void) {
    char name[65]; CHECK(!gethostname(name,sizeof(name)) && !strcmp(name,"banana-os"));
    file="Office-PC\r\n"; CHECK(!gethostname(name,sizeof(name)) && !strcmp(name,"office-pc"));
    struct utsname u; CHECK(!uname(&u) && !strcmp(u.nodename,"office-pc"));
    CHECK(gethostname(name,3)==-1 && errno==ENAMETOOLONG);
    CHECK(gethostname(name,0)==-1 && errno==EINVAL);
    CHECK(gethostname(0,sizeof(name))==-1 && errno==EINVAL);
    file="invalid name\n"; CHECK(!gethostname(name,sizeof(name)) && !strcmp(name,"banana-os"));
    char max[66]; memset(max,'a',63); max[63]='\n'; max[64]=0; file=max;
    CHECK(!gethostname(name,sizeof(name)) && strlen(name)==63);
    max[63]='a'; max[64]='\n'; max[65]=0;
    CHECK(!gethostname(name,sizeof(name)) && !strcmp(name,"banana-os"));
    fail_read=1; CHECK(!gethostname(name,sizeof(name)) && !strcmp(name,"banana-os"));
    printf("PASS: SDK gethostname, uname, normalization, fallbacks and buffer limits\n"); return 0;
}
