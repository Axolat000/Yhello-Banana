#ifndef FRAMEWORK_TEST_H
#define FRAMEWORK_TEST_H
/* The same offline tests run on the host and through selftest browser. */
int framework_tests_run(void (*report)(const char*, int, const char*));
#endif
