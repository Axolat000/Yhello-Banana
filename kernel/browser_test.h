/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BROWSER_TEST_H
#define BROWSER_TEST_H
void browser_test_run(void (*report)(const char*,int,const char*));
void browser_site_test_run(void (*report)(const char*,int,const char*));
void browser_isolation_test_run(void (*report)(const char*,int,const char*));
void browser_compatibility_test_run(int google,void (*report)(const char*,int,const char*));
void browser_libjs_live_test_run(int google,void (*report)(const char*,int,const char*));
void browser_libjs_dom_test_run(void (*report)(const char*,int,const char*));
void browser_libjs_test_run(void (*report)(const char*,int,const char*));
#endif
