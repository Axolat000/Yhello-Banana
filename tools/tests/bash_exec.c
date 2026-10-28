/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define O_RDONLY 0
#define HASH_BANG_BUFSIZ 128
#define EXECUTION_SUCCESS 0
#define EX_NOEXEC 126
#define EX_BINARY_FILE 126
static int failure,errors,closes,checks;
static const char* data;
static int data_len;
#define CHECK(c) do{checks++;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static int mock_open(const char* p,int flags) { if(failure==1){errno=EMFILE;return -1;} return 3; }
static int mock_read(int fd,char* b,int n) {
    if(failure==2){errno=EIO;return -1;}
    if(n>data_len)n=data_len;
    memcpy(b,data,(size_t)n);return n;
}
static int mock_close(int fd) { closes++;return 0; }
static void file_error(const char* p) { CHECK(errno==(failure==1?EMFILE:EIO));errors++; }
static void internal_error(const char* fmt,...) { errors++; }
#define _(s) s
#define open mock_open
#define read mock_read
#define close mock_close
#include "bash_probe.inc"
int main(void) {
    data="\177ELF\002\001\001\0";data_len=8;
    for(failure=0;failure<=2;failure++) {
        errors=closes=0;
        CHECK(probe()==126);
        CHECK(errors==1);
        CHECK(closes==(failure!=1));
    }
    failure=0;errors=closes=0;data="";data_len=0;
    CHECK(probe()==0 && errors==0 && closes==1);
    data="echo valid-script\n";data_len=(int)strlen(data);
    CHECK(probe()==99 && errors==0);
    data="bad\0binary";data_len=10;
    CHECK(probe()==126 && errors==1);
    printf("Bash binary/script probe: %d checks passed\n",checks);
}
