/*
 * LinRTOS - Port layer interface (ARM Cortex-M3/M4/M7).
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_PORT_H
#define RTOS_PORT_H

#include <stdint.h>
#include "kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 🎯 移植层接口：由具体架构实现
 * ============================================================ */

/* 初始化 port 层（PendSV 优先级、FPU Lazy Stacking 等） */
void rtos_port_init(void);

/* 初始化硬件节拍定时器 (SysTick) */
void rtos_port_init_systick(uint32_t tick_hz);

/* 启动第一个任务（触发 SVC 异常，在异常中恢复第一个任务上下文） */
void rtos_port_start_first_task(void);

/* 触发 PendSV 进行上下文切换 */
void rtos_port_trigger_pendsv(void);

/* 初始化任务栈帧（构造首次运行的硬件上下文） */
uint32_t *rtos_port_init_stack(uint32_t *stack_top, rtos_task_func_t func,
                                void *param);

/* ============================================================
 * 🛡️ 临界区（使用 BASEPRI / PRIMASK）
 * ============================================================ */

/* 进入临界区，返回之前的状态/优先级掩码 */
uint32_t rtos_port_enter_critical(void);

/* 退出临界区，恢复之前的状态 */
void rtos_port_exit_critical(uint32_t state);

/* 获取当前中断状态（用于判断是否在ISR中） */
int rtos_port_is_in_isr(void);

/* ============================================================
 * ⚡ 内联辅助：触发 PendSV
 * ============================================================ */

static inline void rtos_port_request_switch(void)
{
    /* 设置 Interrupt Control State Register 的 PENDSVSET */
    *(volatile uint32_t *)0xE000ED04 = 0x10000000;
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
}

#ifdef __cplusplus
}
#endif

#endif /* RTOS_PORT_H */
