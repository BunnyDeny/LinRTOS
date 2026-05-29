/*
 * CmBacktrace configuration for LinRTOS.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef _CMB_CFG_H_
#define _CMB_CFG_H_

/* Include LinRTOS config to get ARCH_CPU_CORTEX_Mx macros */
#include "config.h"
#include "cli_io.h"

#ifdef COMPONENT_CM_BACKTRACE

#include <stdarg.h>

/* Print line: append \r\n to format and call sys_printk once (atomic). */
#define cmb_println cmb_println
static inline int cmb_println(const char *fmt, ...)
{
    char lbuf[160];
    va_list args;
    int len;
    va_start(args, fmt);
    len = vsnprintf(lbuf, sizeof(lbuf) - 2, fmt, args);
    va_end(args);
    if (len <= 0) return len;
    if ((size_t)len >= sizeof(lbuf) - 2) len = sizeof(lbuf) - 3;
    lbuf[len++] = '\r';
    lbuf[len++] = '\n';
    lbuf[len]   = '\0';
    return sys_printk("%s", lbuf);
}

#define cmb_print(fmt, ...) \
    do { \
        sys_printk(fmt, ##__VA_ARGS__); \
    } while(0)

/* Enable OS platform (LinRTOS is an RTOS) */
#define CMB_USING_OS_PLATFORM

/* OS platform type: LinRTOS (must match value in cmb_def.h) */
#define CMB_OS_PLATFORM_TYPE    6

/* CPU platform type mapped from LinRTOS arch config */
#if defined(ARCH_CPU_CORTEX_M3)
    #define CMB_CPU_PLATFORM_TYPE   1
#elif defined(ARCH_CPU_CORTEX_M4)
    #define CMB_CPU_PLATFORM_TYPE   2
#elif defined(ARCH_CPU_CORTEX_M7)
    #define CMB_CPU_PLATFORM_TYPE   3
#elif defined(ARCH_CPU_CORTEX_M33)
    #define CMB_CPU_PLATFORM_TYPE   4
#else
    #error "Unsupported CPU type for CmBacktrace"
#endif

/* Enable dump stack information */
#define CMB_USING_DUMP_STACK_INFO

/* Print language: English */
#define CMB_PRINT_LANGUAGE      0

/* Firmware name for addr2line hint (from Kconfig) */
#ifndef CMB_FIRMWARE_NAME
    #ifdef CM_BACKTRACE_FIRMWARE_NAME
        #define CMB_FIRMWARE_NAME   CM_BACKTRACE_FIRMWARE_NAME
    #else
        #define CMB_FIRMWARE_NAME   "LinRTOS_CmBacktrace"
    #endif
#endif

#endif /* COMPONENT_CM_BACKTRACE */

#endif /* _CMB_CFG_H_ */
