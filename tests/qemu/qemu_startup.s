/*
 * Minimal Cortex-M startup for QEMU mps2-an385.
 */

    .syntax unified
    .thumb

    .section .vectors, "a"
    .global _vectors

_vectors:
    .word __stack_bottom          /* Initial SP */
    .word Reset_Handler           /* Reset */
    .word 0                       /* NMI */
    .word 0                       /* HardFault */
    .word 0                       /* MemManage */
    .word 0                       /* BusFault */
    .word 0                       /* UsageFault */
    .word 0, 0, 0, 0              /* Reserved */
    .word SVC_Handler             /* SVCall */
    .word 0                       /* DebugMon */
    .word 0                       /* Reserved */
    .word PendSV_Handler          /* PendSV */
    .word SysTick_Handler         /* SysTick */

    .section .text
    .thumb_func
    .global Reset_Handler
Reset_Handler:
    ldr r0, =__stack_bottom
    mov sp, r0

    /* 设置 VTOR 指向向量表（允许代码在 RAM/Flash 任意位置运行） */
    ldr r0, =_vectors
    ldr r1, =0xE000ED08
    str r0, [r1]
    dsb
    isb

    bl  main
    b   .
