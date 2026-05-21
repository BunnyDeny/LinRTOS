/*
 * LinRTOS - Tick handler and delay queue management.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#include "linRTOS.h"
#include "kernel.h"
#include "port.h"

/* ============================================================
 * ⏱️ SysTick 中断入口（由用户启动文件调用）
 * ============================================================ */

__attribute__((weak)) void SysTick_Handler(void)
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

    /* ── 处理延时队列 ── */
    struct rtos_tcb *curr = (struct rtos_tcb *)rtos_current_tcb;
    struct rtos_list_node *pos, *n;
    rtos_list_for_each_safe(pos, n, &g_kernel.delay_list) {
        struct rtos_tcb *tcb = rtos_list_entry(pos, struct rtos_tcb, delay_node);
        if ((int32_t)(g_kernel.tick_count - tcb->wake_tick) >= 0) {
            /* 延时到期，唤醒任务 */
            rtos_list_remove(&tcb->delay_node);
            tcb->wake_tick = 0;
            rtos_task_ready(tcb);
            /* 若被唤醒的任务优先级更高，标记需要调度 */
            if (curr && tcb->priority > curr->priority) {
                g_kernel.need_resched = 1;
            }
        } else {
            /* delay_list 按 wake_tick 升序排列，后面的更不会到期 */
            break;
        }
    }

    /* ── 时间片轮转 ── */
#if RTOS_ENABLE_TIME_SLICING
    if (curr && curr != g_kernel.idle_task) {
        rtos_sched_time_slice(curr);
    }
#endif

    RTOS_EXIT_CRITICAL();

    /* 尝试调度（由 rtos_sched 内部决定是否需要上下文切换） */
    rtos_sched();
}
