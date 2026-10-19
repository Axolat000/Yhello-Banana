/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SITE_TEST_H
#define SITE_TEST_H
#include "page.h"
#define BROWSER_SITE_TEST_URL "https://swm.mxtpasontop.fr/"
/* Live public-site checks; out, when non-NULL, receives the page to free. */
int site_tests_run(page_env_t* base,uint32_t memory,
                   void (*report)(const char*,int,const char*),page_t** out);
#endif
