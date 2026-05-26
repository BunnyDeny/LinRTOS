/*
 * LinRTOS - Cortex-M context switch assembly.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

    .syntax unified
    .thumb

#if defined(ARCH_CPU_CORTEX_M33)
    .arch armv8-m.main
#else
    .arch armv7-m
#endif

    .global PendSV_Handler
    .global SVC_Handler
    .global rtos_port_start_first_task

    .extern rtos_current_tcb
    .extern rtos_switch_context

#include "config.h"

/* ============================================================
 * FPU 编译器一致性检查
 * ============================================================
 *
 * 1. 若开启 RTOS_ENABLE_FPU 但编译器未使用 hard-float ABI → 报错
 * 2. 若编译器使用了 hard-float ABI 但未开启 RTOS_ENABLE_FPU → 报错
 *
 * 避免静默失效导致 FPU 寄存器在上下文切换中不被保存/恢复，
 * 或保存代码被编译进去但编译器实际未生成 FPU 指令。
 * ============================================================ */
#if (RTOS_ENABLE_FPU != 0)
    #if !defined(__VFP_FP__)
        #error "RTOS_ENABLE_FPU is enabled but compiler is not using hard-float ABI. Please add -mfloat-abi=hard -mfpu=fpv4-sp-d16 (or fpv5-d16) to your CFLAGS."
    #endif
#else
    #if defined(__VFP_FP__) && !defined(__SOFTFP__)
        #error "RTOS_ENABLE_FPU is 0 but compiler is using hard-float ABI (-mfloat-abi=hard). FPU context will NOT be saved across task switches. Either set RTOS_ENABLE_FPU to 1 in config.h, or remove -mfloat-abi=hard from CFLAGS."
    #endif
#endif

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
    /* 设置 CONTROL.SPSEL = 1，线程模式使用 PSP。
     * 写入 0x02 同时隐含 CONTROL.FPCA = 0，确保首次启动的
     * 任务使用基本异常帧（兼容 RT-Thread 清 FPCA 做法）。
     */
    movs    r0, #0x02
    msr     control, r0
    isb

    /* 加载首个任务的 TCB 与栈指针 */
    ldr     r3, =rtos_current_tcb
    ldr     r1, [r3]
    ldr     r0, [r1]            /* r0 = current_task->stack_ptr */

    /* 恢复软件保存的寄存器 R4-R11 以及 EXC_RETURN(LR)。
     * 伪造栈帧中已预置 EXC_RETURN=0xFFFFFFFD，因此 lr 恢复后
     * 即为正确的基本帧返回码，bx lr 可安全启动首个任务。
     */
    ldmia   r0!, {r4-r11, lr}

    /* 更新 PSP，使其指向硬件自动保存的帧（R0-R3,R12,LR,PC,xPSR） */
    msr     psp, r0
    isb

    bx      lr

/* ============================================================
 * 🔄 PendSV Handler —— 上下文切换
 * ============================================================
 *
 * 由 rtos_port_request_switch() 置位 PENDSVSET 触发。
 * PendSV 优先级应设为最低，确保在所有其他中断处理完毕后执行。
 * 否则Pendsv嵌套其他中断的时候，硬件自动保存/加载错误的msp堆栈指
 * 针将中断上下文的特殊寄存器的内容到任务的堆栈中，从而在任务
 * 引发非法内存访问错误
 *
 * 关键设计变更：不在 C 层预决策下一个任务，而是直接在 PendSV 中
 * 调用 rtos_switch_context() 重新 O(1) 查找最高优先级就绪任务，
 * 并原子更新任务状态与 rtos_current_tcb。这彻底消除了
 * rtos_sched() 与 PendSV 执行之间的竞态窗口。
 *
 * FPU 上下文保存策略（FreeRTOS / RT-Thread 标准做法）：
 *   - 硬件 Lazy Stacking 自动处理 S0-S15 + FPSCR
 *   - 软件仅负责保存/恢复 S16-S31（callee-saved）
 *   - 通过 EXC_RETURN[4] 判断当前任务是否使用了浮点上下文
 */
    .thumb_func
PendSV_Handler:
    /* ---- 保存当前任务上下文 ---- */
    mrs     r0, psp             /* r0 = 当前 PSP */

#if (RTOS_ENABLE_FPU != 0)
    /* 若当前任务使用了 FPU（EXC_RETURN[4] == 0），
     * 将 S16-S31 压入当前任务栈（vstmdb = decrement before store）。
     */
    tst     lr, #0x10           /* LR[4] == 0 ? FPU active? */
    it      eq
    vstmdbeq r0!, {s16-s31}
#endif

    /* 保存 R4-R11 + EXC_RETURN(LR)。
     * LR 中保存了当前任务的 EXC_RETURN 值，其中 bit[4] 标记了
     * 该任务是否使用了浮点上下文，必须在恢复时原样交还。
     */
    stmdb   r0!, {r4-r11, lr}

    /* rtos_current_tcb->stack_ptr = r0 */
    ldr     r3, =rtos_current_tcb
    ldr     r2, [r3]
    str     r0, [r2]

    /* ---- 调用 C 函数完成调度决策与状态更新 ---- */
    /* rtos_switch_context() 在临界区内：
     *   1. 调用 rtos_pick_highest_ready() 重新查找
     *   2. 若 next != curr：更新状态 + rtos_current_tcb
     *   3. 返回 next 指针（在 r0 中）
     * bl 会覆盖 lr，因此先保存 EXC_RETURN。
     */
    push    {lr}
    bl      rtos_switch_context
    mov     r4, r0              /* r4 = next（r4 已在 stmdb 中保存到旧任务栈） */
    pop     {lr}

    /* ---- 恢复下一个任务上下文 ---- */
    /* r0 = next->stack_ptr */
    mov     r0, r4
    ldr     r0, [r0]

    /* 恢复 R4-R11 + EXC_RETURN */
    ldmia   r0!, {r4-r11, lr}

#if (RTOS_ENABLE_FPU != 0)
    /* 若即将恢复的任务使用了 FPU（EXC_RETURN[4] == 0），
     * 从其栈中弹出 S16-S31（vldmia = increment after load）。
     */
    tst     lr, #0x10
    it      eq
    vldmiaeq r0!, {s16-s31}
#endif

    /* 更新 PSP */
    msr     psp, r0
    isb

    /* 使用任务自己保存的 EXC_RETURN（LR）返回。
     * 硬件根据 LR[4] 自动决定恢复基本帧还是扩展帧，
     * 并自动恢复 S0-S15 + FPSCR（若扩展帧）。
     */
    bx      lr

/* ============================================================
 * ▶️ rtos_port_start_first_task
 * ============================================================ */
    .thumb_func
rtos_port_start_first_task:
    svc     #0
    bx      lr
