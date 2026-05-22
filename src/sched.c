/*
 * LinRTOS - Scheduler core.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#include <string.h>
#include "linRTOS.h"
#include "kernel.h"
#include "port.h"

/* ============================================================
 * 🏭 全局状态
 * ============================================================ */

struct rtos_kernel g_kernel;
volatile struct rtos_tcb *rtos_current_tcb = NULL;
static int s_kernel_inited = 0;

/* ============================================================
 * 🔌 内核一次性初始化
 * ============================================================ */

void rtos_kernel_init(void)
{
    if (s_kernel_inited) {
        return;
    }
    s_kernel_inited = 1;
    memset(&g_kernel, 0, sizeof(g_kernel));
    for (int i = 0; i < RTOS_MAX_PRIORITIES; i++) {
        rtos_list_init(&g_kernel.ready_list[i]);
    }
    rtos_list_init(&g_kernel.delay_list);
    rtos_list_init(&g_kernel.terminated_list);
}

/* ============================================================
 * 🔧 就绪队列操作
 * ============================================================ */

void rtos_task_ready(struct rtos_tcb *tcb)
{
    uint32_t prio = tcb->priority;
    rtos_list_insert_before(&g_kernel.ready_list[prio], &tcb->ready_node);
    g_kernel.ready_map |= ((rtos_ready_map_t)1 << prio);
    tcb->state = RTOS_TASK_READY;
}

void rtos_task_unready(struct rtos_tcb *tcb)
{
    uint32_t prio = tcb->priority;
    rtos_list_remove(&tcb->ready_node);
    if (rtos_list_is_empty(&g_kernel.ready_list[prio])) {
        g_kernel.ready_map &= ~((rtos_ready_map_t)1 << prio);
    }
    tcb->state = RTOS_TASK_BLOCKED;
}

/* ============================================================
 * 🎯 选择最高优先级就绪任务
 * ============================================================ */

struct rtos_tcb *rtos_pick_highest_ready(void)
{
    if (g_kernel.ready_map == 0) {
        return g_kernel.idle_task;
    }

#if RTOS_MAX_PRIORITIES <= 32
    int prio = 31 - __builtin_clz((unsigned int)g_kernel.ready_map);
#else
    int prio;
    uint32_t high = (uint32_t)(g_kernel.ready_map >> 32);
    if (high) {
        prio = 63 - __builtin_clz(high);
    } else {
        uint32_t low = (uint32_t)g_kernel.ready_map;
        prio = 31 - __builtin_clz(low);
    }
#endif

    struct rtos_list_node *node = g_kernel.ready_list[prio].next;
    return rtos_list_entry(node, struct rtos_tcb, ready_node);
}

/* ============================================================
 * 🔄 PendSV 中的上下文切换决策
 * ============================================================
 *
 * 由 PendSV_Handler 调用。在临界区内完成：
 * 1. 重新选择最高优先级就绪任务（消除 rtos_sched 与 PendSV 之间的竞态）
 * 2. 更新任务状态（RUNNING → READY，READY → RUNNING）
 * 3. 更新 rtos_current_tcb
 * ============================================================ */

struct rtos_tcb *rtos_switch_context(void)
{
    struct rtos_tcb *curr = (struct rtos_tcb *)rtos_current_tcb;
    RTOS_ENTER_CRITICAL();
    struct rtos_tcb *next = rtos_pick_highest_ready();
    if (next != curr) {
        if (curr && curr->state == RTOS_TASK_RUNNING) {
            curr->state = RTOS_TASK_READY;
        }
        next->state = RTOS_TASK_RUNNING;
        rtos_current_tcb = next;
        RTOS_EXIT_CRITICAL();
    }

    return next;
}

/* ============================================================
 * ⚡ 调度请求
 * ============================================================ */

void rtos_sched(void)
{
    if (!g_kernel.is_running) {
        return;
    }

    RTOS_ENTER_CRITICAL();
    if (g_kernel.sched_lock > 0) {
        g_kernel.need_resched = 1;
        RTOS_EXIT_CRITICAL();
        return;
    }
    g_kernel.need_resched = 0;
    RTOS_EXIT_CRITICAL();

    rtos_port_request_switch();
}

void rtos_sched_yield(void)
{
    RTOS_ENTER_CRITICAL();
    g_kernel.need_resched = 1;
    RTOS_EXIT_CRITICAL();
}

/* ============================================================
 * 🔄 时间片轮转（同优先级）
 * ============================================================ */

#if RTOS_ENABLE_TIME_SLICING
void rtos_sched_time_slice(struct rtos_tcb *tcb)
{
    if (tcb->state != RTOS_TASK_RUNNING && tcb->state != RTOS_TASK_READY) {
        return;
    }
    if (tcb->time_slice > 0) {
        tcb->time_slice--;
        if (tcb->time_slice == 0) {
            /* 时间片耗尽，放到同优先级链表尾部 */
            tcb->time_slice = RTOS_TIME_SLICE_TICKS;
            rtos_list_remove(&tcb->ready_node);
            rtos_list_insert_before(&g_kernel.ready_list[tcb->priority],
                                    &tcb->ready_node);
            /* 如果同优先级还有其他任务，触发调度 */
            if (g_kernel.ready_list[tcb->priority].next != &tcb->ready_node) {
                g_kernel.need_resched = 1;
            }
        }
    }
}
#endif
