/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _LOCALE_H
#define _LOCALE_H
#ifdef __cplusplus
extern "C" {
#endif
/* one locale: "C" (text is UTF-8); locale_t objects all stand for it */
#include <stddef.h>
#define LC_CTYPE    0
#define LC_NUMERIC  1
#define LC_TIME     2
#define LC_COLLATE  3
#define LC_MONETARY 4
#define LC_MESSAGES 5
#define LC_ALL      6
#define LC_CTYPE_MASK    (1 << LC_CTYPE)
#define LC_NUMERIC_MASK  (1 << LC_NUMERIC)
#define LC_TIME_MASK     (1 << LC_TIME)
#define LC_COLLATE_MASK  (1 << LC_COLLATE)
#define LC_MONETARY_MASK (1 << LC_MONETARY)
#define LC_MESSAGES_MASK (1 << LC_MESSAGES)
#define LC_ALL_MASK      0x3f
struct lconv {
    char* decimal_point; char* thousands_sep; char* grouping; char* int_curr_symbol; char* currency_symbol;
    char* mon_decimal_point; char* mon_thousands_sep; char* mon_grouping; char* positive_sign; char* negative_sign;
    char int_frac_digits, frac_digits, p_cs_precedes, p_sep_by_space, n_cs_precedes, n_sep_by_space, p_sign_posn, n_sign_posn;
};
typedef struct __locale_struct* locale_t;
#define LC_GLOBAL_LOCALE ((locale_t)-1L)
char*         setlocale(int cat, const char* name);
struct lconv* localeconv(void);
locale_t      newlocale(int mask, const char* name, locale_t base);
locale_t      duplocale(locale_t l);
void          freelocale(locale_t l);
locale_t      uselocale(locale_t l);
#ifdef __cplusplus
}
#endif
#endif
