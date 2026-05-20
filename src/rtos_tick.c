/*
 * LinRTOS - Tick handler and delay queue management.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#include "rtos.h"
#include "rtos_kernel.h"
#include "rtos_port.h"

/* ============================================================
 * ⏱️ SysTick 中断入口（由用户启动文件调用）
 * ============================================================ */

void SysTick_Handler(void) __attribute__((weak));

void SysTick_Handler(void)
{
    rtos_tick_handler();
}

/* ============================================================
 * 🕐 Tick 处理核心
 * ============================================================ */

void rtos_tick_handler(void)
{
    /* 若内核尚未初始化（如 HAL_Init 阶段 SysTick 已使能），直接返回 */
    if (g_kernel.ready_list[0].next == NULL) {
        return;
    }

    RTOS_ENTER_CRITICAL();

    g_kernel.tick_count++;

    /* ── 1. 处理延时队列 ── */
    struct rtos_list_node *pos, *n;
    rtos_list_for_each_safe(pos, n, &g_kernel.delay_list) {
        struct rtos_tcb *tcb = rtos_list_entry(pos, struct rtos_tcb, delay_node);
        if ((int32_t)(g_kernel.tick_count - tcb->wake_tick) >= 0) {
            /* 超时唤醒：同时从阻塞对象的等待队列中移除 */
            rtos_list_remove(&tcb->delay_node);
            tcb->wake_tick = 0;
            if (tcb->blocking_obj) {
                rtos_list_remove(&tcb->ready_node);
                tcb->blocking_obj = NULL;
            }
            tcb->block_result = RTOS_ERR_TIMEOUT;
            rtos_task_ready(tcb);
        } else {
            /* delay_list 按 wake_tick 升序排列，后面的更不会到期 */
            break;
        }
    }

    /* ── 2. 处理软件定时器 ── */
#if RTOS_ENABLE_SOFT_TIMER
    rtos_timer_tick_handler();
#endif

    /* ── 3. 时间片轮转 ── */
#if RTOS_ENABLE_TIME_SLICING
    struct rtos_tcb *curr = (struct rtos_tcb *)rtos_current_tcb;
    if (curr && curr != g_kernel.idle_task) {
        rtos_sched_time_slice(curr);
    }
#endif

    RTOS_EXIT_CRITICAL();

    /* ── 4. 尝试调度（由 rtos_sched 内部决定是否需要上下文切换） ── */
    if (g_kernel.is_running && !g_kernel.sched_lock) {
        rtos_sched();
    }
}
