#pragma once
#include "types.h"
typedef uint8_t uacpi_u8;
typedef uint16_t uacpi_u16;
typedef uint32_t uacpi_u32;
typedef uint64_t uacpi_u64;
typedef int8_t uacpi_i8;
typedef int16_t uacpi_i16;
typedef int32_t uacpi_i32;
typedef int64_t uacpi_i64;
typedef _Bool bool;
#define true 1
#define false 0
typedef _Bool uacpi_bool;
#define UACPI_TRUE 1
#define UACPI_FALSE 0
#define UACPI_NULL NULL
typedef uintptr_t uacpi_uintptr;
typedef uintptr_t uacpi_virt_addr;
typedef size_t uacpi_size;
typedef __builtin_va_list uacpi_va_list;
#define uacpi_va_start __builtin_va_start
#define uacpi_va_end __builtin_va_end
#define uacpi_va_arg __builtin_va_arg
typedef char uacpi_char;
#define uacpi_offsetof __builtin_offsetof
#define UACPI_PRIu64 "llu"
#define UACPI_PRIx64 "llx"
#define UACPI_PRIX64 "llX"
#define UACPI_FMT64(val) ((unsigned long long)(val))
