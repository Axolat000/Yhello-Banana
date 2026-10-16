#ifndef FRAMEWORK_FIXTURES_H
#define FRAMEWORK_FIXTURES_H
#include "types.h"
/* Pinned offline resources for about:frameworks and the in-OS checks. */
int framework_fixture(const char* url,const char** text,uint32_t* len,const char** type);
#endif
