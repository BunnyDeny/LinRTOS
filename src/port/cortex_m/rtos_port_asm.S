/*
 * LinRTOS - Cortex-M context switch assembly.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

    .syntax unified
    .arch armv7-m
    .thumb

    .global PendSV_Handler
    .global SVC_Handler
    .global rtos_port_start_first_task

    .extern rtos_current_tcb
    .extern rtos_next_tcb

    .text

/* ============================================================
 * 🚀 SVC Handler —— 启动第一个任务
 * ============================================================
 *
 * 由 rtos_scheduler_start() 调用 svc #0 触发。
 * 此时线程模式仍在使用 MSP，需切换为 PSP 并弹出任务上下文。
 */
    .thumb_func
SVC_Handler:
    /* 设置 CONTROL.SPSEL = 1，线程模式使用 PSP */
    movs    r0, #0x02
    msr     control, r0
    isb

    /* 加载首个任务的 TCB 与栈指针 */
    ldr     r3, =rtos_current_tcb
    ldr     r1, [r3]
    ldr     r0, [r1]            /* r0 = current_task->stack_ptr */

    /* 恢复软件保存的寄存器 R4-R11 */
    ldmia   r0!, {r4-r11}

    /* 更新 PSP，使其指向硬件自动保存的帧（R0-R3,R12,LR,PC,xPSR） */
    msr     psp, r0
    isb

    /* EXC_RETURN = 0xFFFFFFFD：返回线程模式，使用 PSP */
    ldr     r0, =0xFFFFFFFD
    bx      r0

/* ============================================================
 * 🔄 PendSV Handler —— 上下文切换
 * ============================================================
 *
 * 由 rtos_port_request_switch() 置位 PENDSVSET 触发。
 * PendSV 优先级应设为最低，确保在所有其他中断处理完毕后执行。
 */
    .thumb_func
PendSV_Handler:
    /* ---- 保存当前任务上下文 ---- */
    mrs     r0, psp             /* r0 = 当前 PSP */
    stmdb   r0!, {r4-r11}       /* 压入 R4-R11 */

    /* rtos_current_tcb->stack_ptr = r0 */
    ldr     r3, =rtos_current_tcb
    ldr     r2, [r3]
    str     r0, [r2]

    /* ---- 切换到下一个任务 ---- */
    /* rtos_current_tcb = rtos_next_tcb */
    ldr     r2, =rtos_next_tcb
    ldr     r2, [r2]
    str     r2, [r3]

    /* g_kernel.current_task = rtos_next_tcb */
    ldr     r3, =g_kernel
    str     r2, [r3, #260]

    /* r0 = new_current_task->stack_ptr */
    ldr     r0, [r2]

    /* 恢复 R4-R11 */
    ldmia   r0!, {r4-r11}

    /* 更新 PSP */
    msr     psp, r0
    isb

    /* 异常返回，线程模式，PSP */
    ldr     r0, =0xFFFFFFFD
    bx      r0

/* ============================================================
 * ▶️ rtos_port_start_first_task
 * ============================================================ */
    .thumb_func
rtos_port_start_first_task:
    svc     #0
    bx      lr
