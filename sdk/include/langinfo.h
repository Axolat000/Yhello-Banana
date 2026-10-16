#ifndef _LANGINFO_H
#define _LANGINFO_H
#ifdef __cplusplus
extern "C" {
#endif
typedef int nl_item;
#define CODESET 14
char* nl_langinfo(nl_item item);   /* CODESET: "UTF-8" */
#ifdef __cplusplus
}
#endif
#endif
