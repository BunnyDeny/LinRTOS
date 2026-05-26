/*
 * CmBacktrace HardFault Handler — Cross-compiler assembly
 *
 * Compatible with:
 *   - GCC / MDK-AC6
 *   - MDK-AC5 (armasm)
 *   - MDK-AC6 (armclang, GNU-syntax)
 *   - IAR Embedded Workbench
 *
 * Uses Kconfig macros from config.h to select the correct syntax.
 */

#include "config.h"

#ifdef COMPONENT_CM_BACKTRACE

/* ============================================================
 * Compiler-specific section directives and symbol exports
 * ============================================================ */
#if defined(ARCH_COMPILER_GCC) || defined(ARCH_MDK_AC6)
    /* GCC / MDK-AC6 */
    .syntax unified
    .thumb
    .text
    .align 2
    .global HardFault_Handler
    .type HardFault_Handler, %function
#elif defined(ARCH_MDK_AC5)
    /* MDK-AC5 (armasm) */
    AREA |.text|, CODE, READONLY, ALIGN=2
    THUMB
    EXPORT HardFault_Handler
    IMPORT cm_backtrace_fault
#elif defined(ARCH_COMPILER_IAR)
    /* IAR Embedded Workbench */
    SECTION .text:CODE
    THUMB
    PUBLIC HardFault_Handler
#endif

/* ============================================================
 * HardFault_Handler entry
 * ============================================================ */
HardFault_Handler:
#if defined(ARCH_MDK_AC5)
    PROC
#endif
    MOV     r0, lr
    MOV     r1, sp
    BL      cm_backtrace_fault

#if defined(ARCH_COMPILER_GCC) || defined(ARCH_MDK_AC6)
Fault_Loop:
    B       Fault_Loop
#elif defined(ARCH_MDK_AC5)
Fault_Loop
    B       Fault_Loop
#elif defined(ARCH_COMPILER_IAR)
Fault_Loop
    B       Fault_Loop
#endif

#if defined(ARCH_MDK_AC5)
    ENDP
#endif

/* ============================================================
 * Section / file termination
 * ============================================================ */
#if defined(ARCH_COMPILER_GCC) || defined(ARCH_MDK_AC6)
    .size HardFault_Handler, . - HardFault_Handler
#elif defined(ARCH_MDK_AC5)
    END
#elif defined(ARCH_COMPILER_IAR)
    END
#endif

#endif /* COMPONENT_CM_BACKTRACE */
