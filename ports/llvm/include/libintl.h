#ifndef BANANA_LLVM_LIBINTL_H
#define BANANA_LLVM_LIBINTL_H
#ifdef __cplusplus
extern "C" {
#endif
char* gettext(const char*); char* dgettext(const char*, const char*);
char* dcgettext(const char*, const char*, int);
char* ngettext(const char*, const char*, unsigned long);
char* dngettext(const char*, const char*, const char*, unsigned long);
char* dcngettext(const char*, const char*, const char*, unsigned long, int);
char* textdomain(const char*); char* bindtextdomain(const char*, const char*);
char* bind_textdomain_codeset(const char*, const char*);
#ifdef __cplusplus
}
#endif
#endif
