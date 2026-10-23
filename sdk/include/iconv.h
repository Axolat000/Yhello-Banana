/* SPDX-License-Identifier: BSD-2-Clause */
/* <iconv.h>: converting text between character encodings - UTF-8, ASCII,
 * ISO-8859-1 (Latin-1), UTF-16 / UCS-2 and UTF-32 / UCS-4 (each also LE /
 * BE), WCHAR_T; a name may end in //TRANSLIT (unconvertible characters
 * become '?') or //IGNORE (they are left out) */
#ifndef _ICONV_H
#define _ICONV_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void* iconv_t;
iconv_t iconv_open(const char* tocode, const char* fromcode);
size_t  iconv(iconv_t cd, char** in, size_t* inleft, char** out, size_t* outleft);
int     iconv_close(iconv_t cd);
#ifdef __cplusplus
}
#endif
#endif
