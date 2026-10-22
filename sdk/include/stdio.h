/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _STDIO_H
#define _STDIO_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>

/* Banana OS stdio: buffered streams over the system's descriptors (line
 * buffered on a terminal, stderr unbuffered); stdin, stdout and stderr are
 * descriptors 0, 1 and 2 - the terminal the app runs in, or a pipe, a file. */

#define EOF      (-1)
#define BUFSIZ   8192
#define L_tmpnam 32
#define P_tmpdir "/tmp"
#define TMP_MAX  238328
#define FILENAME_MAX 512
#define FOPEN_MAX    64
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct _bfile FILE;
typedef off_t fpos_t;
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

int    printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
int    fprintf(FILE* f, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
int    sprintf(char* buf, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
int    snprintf(char* buf, size_t n, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
int    vprintf(const char* fmt, va_list ap);
int    vfprintf(FILE* f, const char* fmt, va_list ap);
int    vsprintf(char* buf, const char* fmt, va_list ap);
int    vsnprintf(char* buf, size_t n, const char* fmt, va_list ap);
int    asprintf(char** out, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
int    vasprintf(char** out, const char* fmt, va_list ap);
FILE*  popen(const char* cmd, const char* mode);     /* (no shell for apps: NULL) */
int    pclose(FILE* f);
int    scanf(const char* fmt, ...);
int    sscanf(const char* s, const char* fmt, ...);
int    fscanf(FILE* f, const char* fmt, ...);
int    vsscanf(const char* s, const char* fmt, va_list ap);
int    vfscanf(FILE* f, const char* fmt, va_list ap);
int    putchar(int c);
int    puts(const char* s);
int    getchar(void);
char*  fgets(char* buf, int n, FILE* f);
int    fputs(const char* s, FILE* f);
int    fputc(int c, FILE* f);
int    putc(int c, FILE* f);
int    fgetc(FILE* f);
int    getc(FILE* f);
int    ungetc(int c, FILE* f);
FILE*  fopen(const char* path, const char* mode);
FILE*  open_memstream(char** buf, size_t* size);              /* writing into memory (malloc'd) */
FILE*  fmemopen(void* buf, size_t size, const char* mode);   /* reading or writing a buffer */
int    fclose(FILE* f);
size_t fread(void* buf, size_t size, size_t n, FILE* f);
size_t fwrite(const void* buf, size_t size, size_t n, FILE* f);
int    fseek(FILE* f, long off, int whence);
long   ftell(FILE* f);
void   rewind(FILE* f);
int    feof(FILE* f);
int    ferror(FILE* f);
void   clearerr(FILE* f);
int    fflush(FILE* f);
int    remove(const char* path);
int    rename(const char* from, const char* to);
int    renameat(int fromfd, const char* from, int tofd, const char* to);
int    renameat2(int fromfd, const char* from, int tofd, const char* to, unsigned flags);
#define RENAME_NOREPLACE 1
#define RENAME_EXCHANGE  2               /* (not done: EINVAL) */
#define RENAME_WHITEOUT  4
void   perror(const char* s);
FILE*  fdopen(int fd, const char* mode);
long   getdelim(char** line, size_t* cap, int delim, FILE* f);    /* (ssize_t) */
long   getline(char** line, size_t* cap, FILE* f);
void   flockfile(FILE* f);
void   funlockfile(FILE* f);
int    ftrylockfile(FILE* f);
int    fileno(FILE* f);
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
int    setvbuf(FILE* f, char* buf, int mode, size_t size);
void   setbuf(FILE* f, char* buf);
void   setbuffer(FILE* f, char* buf, size_t size);
void   setlinebuf(FILE* f);
FILE*  freopen(const char* path, const char* mode, FILE* f);
int    fseeko(FILE* f, off_t off, int whence);
off_t  ftello(FILE* f);
int    fgetpos(FILE* f, fpos_t* pos);
int    fsetpos(FILE* f, const fpos_t* pos);
FILE*  tmpfile(void);
int    dprintf(int fd, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
int    vdprintf(int fd, const char* fmt, va_list ap);
int    getc_unlocked(FILE* f);
int    getchar_unlocked(void);
int    fgetc_unlocked(FILE* f);
int    putc_unlocked(int c, FILE* f);
int    fputc_unlocked(int c, FILE* f);
int    putchar_unlocked(int c);
int    fputs_unlocked(const char* s, FILE* f);
size_t fwrite_unlocked(const void* buf, size_t size, size_t n, FILE* f);
size_t fread_unlocked(void* buf, size_t size, size_t n, FILE* f);
char*  fgets_unlocked(char* buf, int n, FILE* f);
int    fflush_unlocked(FILE* f);
int    feof_unlocked(FILE* f);
int    ferror_unlocked(FILE* f);
void   clearerr_unlocked(FILE* f);
int    fileno_unlocked(FILE* f);
int    fpurge(FILE* f);
#ifdef __cplusplus
}
#endif
#endif
