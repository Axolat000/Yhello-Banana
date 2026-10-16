#ifndef BROWSER_TEST_H
#define BROWSER_TEST_H
void browser_test_run(void (*report)(const char*,int,const char*));
void browser_site_test_run(void (*report)(const char*,int,const char*));
#endif
