/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include "../../kernel/timer_math.h"
static int failed,checks;
static void report(const char* label,int ok,const char* note) {
    (void)note;checks++;failed+=!ok;printf("%s %s\n",ok?"ok  ":"FAIL",label);
}
int main(void) {timer_math_tests(report);printf("Clock: %d checks, %d failed\n",checks,failed);return !!failed;}
