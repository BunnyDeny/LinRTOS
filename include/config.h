/*
 * LinRTOS - Lightweight preemptive RTOS for ARM Cortex-M.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

/* Kconfig generated configuration — must be generated before build */
#include "linrtos_kconfig.h"

/* ============================================================
 * 🔒 固定配置（不参与 Kconfig）
 * ============================================================ */

/* 栈填充魔数（用于栈溢出检测） */
#ifndef RTOS_STACK_FILL_MAGIC
#define RTOS_STACK_FILL_MAGIC       0xA5A5A5A5U
#endif

/* ============================================================
 * 🏗️ Architecture / CPU / Compiler 映射
 * ============================================================
 *
 * 以下宏根据 Kconfig 生成的 ARCH_* 符号映射为源码通用的
 * 标识宏，保证向后兼容并方便后续 CmBacktrace 等组件集成。
 */

/* ---- CPU Type ---- */
#if defined(ARCH_CPU_CORTEX_M3)
    #define LINRTOS_CPU_CORTEX_M3       1
#elif defined(ARCH_CPU_CORTEX_M4)
    #define LINRTOS_CPU_CORTEX_M4       1
#elif defined(ARCH_CPU_CORTEX_M7)
    #define LINRTOS_CPU_CORTEX_M7       1
#elif defined(ARCH_CPU_CORTEX_M33)
    #define LINRTOS_CPU_CORTEX_M33      1
#endif

/* ---- FPU (向后兼容旧源码中的 RTOS_ENABLE_FPU) ---- */
#if defined(ARCH_ENABLE_FPU)
    #define RTOS_ENABLE_FPU             1
#endif

/* ---- Compiler Toolchain ----
 *
 * 这些宏通常由编译器自身预定义；此处仅在未被编译器定义时
 * 根据 Kconfig 选择进行补充，方便条件编译和第三方库识别。
 *
 * AC6 的 __ARMCC_VERSION 与 __clang__ 由编译器自带真实值，
 * 因此使用 #ifndef 保护，避免覆盖。
 */
#if defined(ARCH_COMPILER_GCC)
    #ifndef __GNUC__
    #define __GNUC__                    1
    #endif

#elif defined(ARCH_COMPILER_MDK)
    #if defined(ARCH_MDK_AC5)
        /* Legacy ARM Compiler 5 (armcc) */
        #ifndef __CC_ARM
        #define __CC_ARM                1
        #endif
    #elif defined(ARCH_MDK_AC6)
        /* ARM Compiler 6 (Clang-based).
         * AC6 自身已定义 __ARMCC_VERSION（>= 6010050）和 __clang__。
         * 仅在编译器未定义时做兜底补充（例如用 GCC 模拟测试场景）。
         */
        #ifndef __ARMCC_VERSION
        #define __ARMCC_VERSION         6010000
        #endif
        #ifndef __clang__
        #define __clang__               1
        #endif
    #endif

#elif defined(ARCH_COMPILER_IAR)
    #ifndef __ICCARM__
    #define __ICCARM__                  1
    #endif
#endif

#endif /* RTOS_CONFIG_H */
