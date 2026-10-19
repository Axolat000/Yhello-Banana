/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ACPI_SLEEP_H
#define ACPI_SLEEP_H
#include "types.h"
/* Static Name(Package) sleep declarations, not a general AML interpreter. */
int acpi_sleep_types(const uint8_t* aml,uint32_t len,const char name[4],uint16_t* a,uint16_t* b);
/* A status query never enters sleep. A nonzero seconds arms an RTC wake. */
int acpi_s3_status(char* reason,int cap);
int acpi_s3_suspend(unsigned seconds,char* reason,int cap);
unsigned acpi_s3_resumes(void);
void acpi_sleep_tests(void (*report)(const char*,int,const char*));
#endif
