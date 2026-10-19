#include <stdio.h>
static int checks,failed;
static void report(const char* s,int ok,const char* note) {
    printf("%s %s%s%s\n",ok?"ok  ":"FAIL",s,*note?" - ":"",note);checks++;failed+=!ok;
}
extern void acpi_sleep_tests(void (*)(const char*,int,const char*));
int main(void) { acpi_sleep_tests(report);printf("ACPI sleep: %d checks, %d failed\n",checks,failed);return !!failed; }
