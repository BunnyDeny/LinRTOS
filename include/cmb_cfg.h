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

/* Print line: map to LinRTOS debug_printf */
#define cmb_println(...) \
    do { \
        extern void debug_printf(const char *fmt, ...); \
        debug_printf(__VA_ARGS__); \
        debug_printf("\r\n"); \
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

#endif /* _CMB_CFG_H_ */
