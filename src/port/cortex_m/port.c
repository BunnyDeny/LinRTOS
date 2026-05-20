/*
 * LinRTOS - Cortex-M port layer (C part).
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#include <stdint.h>
#include "port.h"
#include "kernel.h"
#include "task.h"

/* ============================================================
 * ⚡ SysTick 初始化
 * ============================================================ */

/* 若用户未定义 SystemCoreClock，提供一个弱默认 */
__attribute__((weak)) extern uint32_t SystemCoreClock;

static uint32_t s_core_clock = 0;

void rtos_port_init_systick(uint32_t tick_hz)
{
    if (s_core_clock == 0) {
        s_core_clock = (SystemCoreClock) ? SystemCoreClock : 8000000U;
    }

    uint32_t reload = s_core_clock / tick_hz - 1;

    /* SysTick 寄存器地址 */
    volatile uint32_t *ctrl  = (volatile uint32_t *)0xE000E010;
    volatile uint32_t *load  = (volatile uint32_t *)0xE000E014;
    volatile uint32_t *val   = (volatile uint32_t *)0xE000E018;

    *load = reload;
    *val  = 0;
    /* ENABLE(1) | TICKINT(2) | CLKSOURCE(4) */
    *ctrl = 0x07;
}

/* 允许用户手动设置 core clock */
void rtos_port_set_core_clock(uint32_t clock_hz)
{
    s_core_clock = clock_hz;
}

/* ============================================================
 * 🛡️ 临界区（使用 PRIMASK，全架构兼容）
 * ============================================================ */

uint32_t rtos_port_enter_critical(void)
{
    uint32_t primask;
    __asm volatile (
        "mrs %0, primask\n"
        "cpsid i\n"
        : "=r" (primask)
        :
        : "memory"
    );
    return primask;
}

void rtos_port_exit_critical(uint32_t state)
{
    __asm volatile (
        "msr primask, %0\n"
        :: "r" (state)
        : "memory"
    );
}

/* ============================================================
 * 📍 中断状态判断
 * ============================================================ */

int rtos_port_is_in_isr(void)
{
    /* IPSR: 0=线程模式，非0=异常/中断 */
    uint32_t ipsr;
    __asm volatile ("mrs %0, ipsr" : "=r" (ipsr));
    return (ipsr != 0);
}

/* ============================================================
 * 💾 初始化任务栈帧（构造首次运行的硬件上下文）
 * ============================================================ */

/* 任务返回地址（删除自身） */
static void rtos_task_exit_trampoline(void)
{
    rtos_task_delete(NULL);
}

uint32_t *rtos_port_init_stack(uint32_t *stack_top,
                                rtos_task_func_t func,
                                void *param)
{
    /* ARM AAPCS 要求栈在公共接口处 8 字节对齐；
     * 异常发生时硬件自动保存 8 个寄存器（32字节），
     * 若 PSP 未对齐会自动插入填充。为确保我们构造的
     * 人工栈帧与硬件行为一致，先把栈顶强制 8 字节对齐。
     */
    stack_top = (uint32_t *)(((uint32_t)stack_top) & ~7U);

    stack_top--;                    /* xPSR */
    *stack_top = 0x01000000;        /* Thumb 位必须置 1 */

    stack_top--;                    /* PC */
    *stack_top = (uint32_t)func;

    stack_top--;                    /* LR */
    *stack_top = (uint32_t)rtos_task_exit_trampoline;

    stack_top--;                    /* R12 */
    *stack_top = 0;

    stack_top--;                    /* R3 */
    *stack_top = 0;

    stack_top--;                    /* R2 */
    *stack_top = 0;

    stack_top--;                    /* R1 */
    *stack_top = 0;

    stack_top--;                    /* R0 */
    *stack_top = (uint32_t)param;

    /* 软件保存部分 */
    stack_top--;                    /* R11 */
    *stack_top = 0;

    stack_top--;                    /* R10 */
    *stack_top = 0;

    stack_top--;                    /* R9 */
    *stack_top = 0;

    stack_top--;                    /* R8 */
    *stack_top = 0;

    stack_top--;                    /* R7 */
    *stack_top = 0;

    stack_top--;                    /* R6 */
    *stack_top = 0;

    stack_top--;                    /* R5 */
    *stack_top = 0;

    stack_top--;                    /* R4 */
    *stack_top = 0;

    return stack_top;
}
