/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _CTYPE_H
#define _CTYPE_H
#ifdef __cplusplus
extern "C" {
#endif
int isalnum(int c);
int isalpha(int c);
int isdigit(int c);
int isxdigit(int c);
int islower(int c);
int isupper(int c);
int isspace(int c);
int ispunct(int c);
int isprint(int c);
int isgraph(int c);
int iscntrl(int c);
int tolower(int c);
int toupper(int c);
#ifdef __cplusplus
}
#endif
#endif
