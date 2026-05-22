/*
 * LinRTOS - Task management.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#include <string.h>
#include "linRTOS.h"
#include "kernel.h"
#include "port.h"

extern volatile struct rtos_tcb *rtos_current_tcb;

/* ============================================================
 * 🏭 TCB 静态池
 * ============================================================ */

#ifndef RTOS_MAX_TASKS
#define RTOS_MAX_TASKS      16
#endif

static struct rtos_tcb s_tcb_pool[RTOS_MAX_TASKS];
static uint32_t s_tcb_used_mask = 0;

/* ============================================================
 * 🔧 TCB 分配与释放
 * ============================================================ */

static struct rtos_tcb *rtos_tcb_alloc(void)
{
    RTOS_ENTER_CRITICAL();
    for (int i = 0; i < RTOS_MAX_TASKS; i++) {
        if (!(s_tcb_used_mask & (1U << i))) {
            s_tcb_used_mask |= (1U << i);
            struct rtos_tcb *tcb = &s_tcb_pool[i];
            memset(tcb, 0, sizeof(*tcb));
            rtos_list_init(&tcb->ready_node);
            rtos_list_init(&tcb->delay_node);
            RTOS_EXIT_CRITICAL();
            return tcb;
        }
    }
    RTOS_EXIT_CRITICAL();
    return NULL;
}

static void rtos_tcb_free(struct rtos_tcb *tcb)
{
    int idx = (int)(tcb - s_tcb_pool);
    if (idx >= 0 && idx < RTOS_MAX_TASKS) {
        RTOS_ENTER_CRITICAL();
        s_tcb_used_mask &= ~(1U << idx);
        RTOS_EXIT_CRITICAL();
    }
}

/* ============================================================
 * 🪶 空闲任务
 * ============================================================ */

#ifndef RTOS_IDLE_STACK_SIZE
#define RTOS_IDLE_STACK_SIZE    64
#endif

static uint32_t s_idle_stack[RTOS_IDLE_STACK_SIZE];
static struct rtos_tcb s_idle_tcb;

void rtos_idle_task(void *param)
{
    (void)param;
    for (;;) {
        /* 每次只清理一个已终止的任务，避免长时间关中断 */
        RTOS_ENTER_CRITICAL();
        if (!rtos_list_is_empty(&g_kernel.terminated_list)) {
            struct rtos_list_node *node = g_kernel.terminated_list.next;
            rtos_list_remove(node);
            struct rtos_tcb *tcb = rtos_list_entry(node, struct rtos_tcb, delay_node);
            RTOS_EXIT_CRITICAL();
            rtos_tcb_free(tcb);
            continue;
        }
        RTOS_EXIT_CRITICAL();

#if RTOS_ENABLE_IDLE_HOOK
        if (g_kernel.idle_hook) {
            g_kernel.idle_hook();
        }
#endif
        __asm volatile ("wfi");
    }
}

/* ============================================================
 * 📏 栈辅助
 * ============================================================ */

static void rtos_stack_fill(uint32_t *stack, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        stack[i] = RTOS_STACK_FILL_MAGIC;
    }
}

/* ============================================================
 * 🚀 任务创建
 * ============================================================ */

rtos_err_t rtos_task_create(rtos_task_func_t func, const char *name,
                            uint32_t *stack_buffer, uint32_t stack_depth_words,
                            void *param, uint32_t priority,
                            rtos_task_handle_t *out_handle)
{
    rtos_kernel_init();
    if (!func || !stack_buffer || stack_depth_words < 32) {
        return RTOS_ERR_PARAM;
    }
    if (priority >= RTOS_MAX_PRIORITIES) {
        return RTOS_ERR_PARAM;
    }

    struct rtos_tcb *tcb = rtos_tcb_alloc();
    if (!tcb) {
        return RTOS_ERR_NOMEM;
    }

    strncpy(tcb->name, name ? name : "", RTOS_MAX_TASK_NAME_LEN - 1);
    tcb->name[RTOS_MAX_TASK_NAME_LEN - 1] = '\0';

    tcb->stack_base = stack_buffer;
    tcb->stack_size = stack_depth_words;
    tcb->stack_top = stack_buffer + stack_depth_words;
    tcb->priority = priority;
    tcb->time_slice = RTOS_TIME_SLICE_TICKS;
    tcb->state = RTOS_TASK_READY;

    rtos_stack_fill(stack_buffer, stack_depth_words);
    tcb->stack_ptr = rtos_port_init_stack(tcb->stack_top, func, param);

    RTOS_ENTER_CRITICAL();
    rtos_task_ready(tcb);
    RTOS_EXIT_CRITICAL();

    if (out_handle) {
        *out_handle = tcb;
    }

    if (g_kernel.is_running && tcb->priority > ((struct rtos_tcb *)rtos_current_tcb)->priority) {
        rtos_sched();
    }

    return RTOS_OK;
}

/* ============================================================
 * 🗑️ 任务删除
 * ============================================================ */

void rtos_task_delete(rtos_task_handle_t task)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)task;

    if (!tcb) {
        tcb = (struct rtos_tcb *)rtos_current_tcb;
    }

    RTOS_ENTER_CRITICAL();

    if (tcb->state == RTOS_TASK_READY || tcb->state == RTOS_TASK_RUNNING) {
        rtos_task_unready(tcb);
    } else if (tcb->state == RTOS_TASK_BLOCKED) {
        rtos_list_remove(&tcb->delay_node);
    }

    tcb->state = RTOS_TASK_DELETED;

    if (tcb == (struct rtos_tcb *)rtos_current_tcb) {
        /* 自删：强制释放所有嵌套的调度锁，挂到待回收列表，然后触发调度 */
        g_kernel.need_resched = 1;
        while (g_kernel.sched_lock > 0) {
            rtos_sched_unlock();
        }
        /* 将自删任务挂到 terminated_list，由空闲任务后续回收 TCB */
        rtos_list_insert_before(&g_kernel.terminated_list, &tcb->delay_node);
        RTOS_EXIT_CRITICAL();
        rtos_sched();
        /* 理论上不会执行到这里 */
        for (;;)
            __asm volatile ("wfi");
    }

    rtos_tcb_free(tcb);
    RTOS_EXIT_CRITICAL();
}

/* ============================================================
 * ⏸️ 挂起 / 恢复
 * ============================================================ */

void rtos_task_suspend(rtos_task_handle_t task)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)task;
    if (!tcb) {
        tcb = (struct rtos_tcb *)rtos_current_tcb;
    }

    RTOS_ENTER_CRITICAL();
    if (tcb->state == RTOS_TASK_READY || tcb->state == RTOS_TASK_RUNNING) {
        rtos_task_unready(tcb);
        tcb->state = RTOS_TASK_SUSPENDED;
        if (tcb == (struct rtos_tcb *)rtos_current_tcb) {
            RTOS_EXIT_CRITICAL();
            rtos_sched();
            return;
        }
    } else if (tcb->state == RTOS_TASK_BLOCKED) {
        rtos_list_remove(&tcb->delay_node);
        tcb->state = RTOS_TASK_SUSPENDED;
    }
    RTOS_EXIT_CRITICAL();
}

void rtos_task_resume(rtos_task_handle_t task)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)task;
    if (!tcb) {
        return;
    }

    RTOS_ENTER_CRITICAL();
    if (tcb->state == RTOS_TASK_SUSPENDED) {
        rtos_task_ready(tcb);
        /* 如果被恢复的任务优先级高于当前任务，触发抢占 */
        if (tcb->priority > ((struct rtos_tcb *)rtos_current_tcb)->priority) {
            RTOS_EXIT_CRITICAL();
            rtos_sched();
            return;
        }
    }
    RTOS_EXIT_CRITICAL();
}

rtos_err_t rtos_task_abort_delay(rtos_task_handle_t task)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)task;
    if (!tcb) {
        return RTOS_ERR_PARAM;
    }

    RTOS_ENTER_CRITICAL();
    if (tcb->state != RTOS_TASK_BLOCKED) {
        RTOS_EXIT_CRITICAL();
        return RTOS_ERR_STATE;
    }

    rtos_list_remove(&tcb->delay_node);
    tcb->wake_tick = 0;
    rtos_task_ready(tcb);
    RTOS_EXIT_CRITICAL();

    rtos_sched();
    return RTOS_OK;
}

/* ============================================================
 * ⏰ 延时 / Yield
 * ============================================================ */

void rtos_task_delay(uint32_t ticks)
{
    if (ticks == 0) {
        rtos_task_yield();
        return;
    }

    struct rtos_tcb *tcb = (struct rtos_tcb *)rtos_current_tcb;

    RTOS_ENTER_CRITICAL();
    tcb->wake_tick = g_kernel.tick_count + ticks;
    tcb->state = RTOS_TASK_BLOCKED;
    rtos_task_unready(tcb);

    /* 按 wake_tick 升序插入延时队列 */
    struct rtos_list_node *pos;
    rtos_list_for_each(pos, &g_kernel.delay_list) {
        struct rtos_tcb *p = rtos_list_entry(pos, struct rtos_tcb, delay_node);
        if ((int32_t)(p->wake_tick - tcb->wake_tick) > 0) {
            break;
        }
    }
    rtos_list_insert_before(pos, &tcb->delay_node);

    RTOS_EXIT_CRITICAL();
    rtos_sched();
}

void rtos_task_delay_until(uint32_t *prev_wake_tick, uint32_t interval)
{
    if (!prev_wake_tick || interval == 0) {
        return;
    }

    uint32_t next_wake = *prev_wake_tick + interval;
    uint32_t now = rtos_get_tick_count();

    if ((int32_t)(next_wake - now) > 0) {
        rtos_task_delay(next_wake - now);
    }

    *prev_wake_tick = next_wake;
}

void rtos_task_yield(void)
{
    RTOS_ENTER_CRITICAL();
#if RTOS_ENABLE_TIME_SLICING
    /* 将当前任务放到同优先级链表尾部，重置时间片 */
    struct rtos_tcb *tcb = (struct rtos_tcb *)rtos_current_tcb;
    tcb->time_slice = RTOS_TIME_SLICE_TICKS;
    rtos_list_remove(&tcb->ready_node);
    rtos_list_insert_before(&g_kernel.ready_list[tcb->priority],
                            &tcb->ready_node);
#endif
    g_kernel.need_resched = 1;
    RTOS_EXIT_CRITICAL();
    rtos_sched();
}

/* ============================================================
 * 📊 查询接口
 * ============================================================ */

rtos_task_handle_t rtos_task_get_current(void)
{
    return (struct rtos_tcb *)rtos_current_tcb;
}

uint32_t rtos_task_get_priority(rtos_task_handle_t task)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)task;
    if (!tcb) {
        tcb = (struct rtos_tcb *)rtos_current_tcb;
    }
    return tcb ? tcb->priority : 0;
}

void rtos_task_set_priority(rtos_task_handle_t task, uint32_t priority)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)task;
    if (!tcb) {
        tcb = (struct rtos_tcb *)rtos_current_tcb;
    }
    if (!tcb || priority >= RTOS_MAX_PRIORITIES) {
        return;
    }

    RTOS_ENTER_CRITICAL();
    tcb->priority = priority;

    if (tcb->state == RTOS_TASK_READY || tcb->state == RTOS_TASK_RUNNING) {
        rtos_task_unready(tcb);
        rtos_task_ready(tcb);
    }

    RTOS_EXIT_CRITICAL();
    rtos_sched();
}

rtos_task_state_t rtos_task_get_state(rtos_task_handle_t task)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)task;
    if (!tcb) {
      tcb = (struct rtos_tcb *)rtos_current_tcb;
    }
    return tcb->state;
  }

uint32_t rtos_task_get_stack_free(rtos_task_handle_t task)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)task;
    if (!tcb) {
        tcb = (struct rtos_tcb *)rtos_current_tcb;
    }
    if (!tcb) {
        return 0;
    }

    uint32_t *p = tcb->stack_base;
    uint32_t free = 0;
    while (free < tcb->stack_size && *p == RTOS_STACK_FILL_MAGIC) {
        p++;
        free++;
    }
    return free;
}

uint32_t rtos_get_tick_count(void)
{
    return g_kernel.tick_count;
}

int rtos_scheduler_is_running(void)
{
    return g_kernel.is_running;
}

/* ============================================================
 * 🔒 调度锁
 * ============================================================ */

void rtos_sched_lock(void)
{
    RTOS_ENTER_CRITICAL();
    g_kernel.sched_lock++;
    RTOS_EXIT_CRITICAL();
}

void rtos_sched_unlock(void)
{
    RTOS_ENTER_CRITICAL();
    if (g_kernel.sched_lock > 0) {
        g_kernel.sched_lock--;
        if (g_kernel.sched_lock == 0 && g_kernel.need_resched) {
            g_kernel.need_resched = 0;
            RTOS_EXIT_CRITICAL();
            rtos_sched();
            return;
        }
    }
    RTOS_EXIT_CRITICAL();
}

/* ============================================================
 * 🎬 调度器启动（不会返回）
 * ============================================================ */

static void rtos_create_idle_task(void)
{
    memset(&s_idle_tcb, 0, sizeof(s_idle_tcb));
    strncpy(s_idle_tcb.name, "idle", RTOS_MAX_TASK_NAME_LEN - 1);
    s_idle_tcb.stack_base = s_idle_stack;
    s_idle_tcb.stack_size = sizeof(s_idle_stack) / sizeof(uint32_t);
    s_idle_tcb.stack_top = s_idle_stack + s_idle_tcb.stack_size;
    s_idle_tcb.priority = 0;
    s_idle_tcb.time_slice = RTOS_TIME_SLICE_TICKS;
    s_idle_tcb.state = RTOS_TASK_READY;

    rtos_stack_fill(s_idle_stack, s_idle_tcb.stack_size);
    s_idle_tcb.stack_ptr = rtos_port_init_stack(s_idle_tcb.stack_top,
                                                 rtos_idle_task, NULL);
    rtos_list_init(&s_idle_tcb.ready_node);
    rtos_list_init(&s_idle_tcb.delay_node);
    rtos_task_ready(&s_idle_tcb);
    g_kernel.idle_task = &s_idle_tcb;
}

void rtos_scheduler_start(void)
{
    /* 初始化 port 层：PendSV/SysTick 优先级、FPU 等 */
    rtos_port_init();

    rtos_kernel_init();

    /* 创建空闲任务 */
    rtos_create_idle_task();

    /* 初始化节拍 */
    rtos_port_init_systick(RTOS_TICK_RATE_HZ);

    /* 选出第一个运行的任务 */
    struct rtos_tcb *first = rtos_pick_highest_ready();
    first->state = RTOS_TASK_RUNNING;
    g_kernel.is_running = 1;
    rtos_current_tcb = first;

    /* 启动第一个任务（触发 SVC） */
    rtos_port_start_first_task();

    /* 永远不会到达 */
    for (;;) {
        __asm volatile ("wfi");
    }
}
