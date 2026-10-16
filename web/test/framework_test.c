#include <stdio.h>
#include "framework_test.h"
void host_fonts(void);
static int passed,failed;
static void report(const char* name,int ok,const char* note) {
    printf("%s %s%s%s\n",ok?"ok  ":"FAIL",name,*note?" - ":"",note);
    fflush(stdout);if(ok)passed++;else failed++;
}
int main(void) {
    host_fonts();int rc=framework_tests_run(report);
    printf("browser: %d passed, %d failed\n",passed,failed);
    return rc || failed ? 1 : 0;
}
