/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include "../../kernel/bootmem_test.h"
static int checks,failed;
static void report(const char* label,int ok,const char* note) {
    checks++;failed+=!ok;printf("%s %s%s%s\n",ok?"ok  ":"FAIL",label,*note?" - ":"",note);
}
int main(void) {
    bootmem_tests_run(report);
    printf("Boot memory: %d checks, %d failed\n",checks,failed);
    return !!failed;
}
