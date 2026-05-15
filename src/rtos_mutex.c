/*
 * LinRTOS - Recursive mutex with priority inheritance.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#include <string.h>
#include "rtos.h"
#include "rtos_kernel.h"
#include "rtos_port.h"

#define RTOS_MAX_MUTEXES    16

struct rtos_mutex {
    struct rtos_list_node wait_list;
    struct rtos_tcb *holder;
    uint32_t recursion;
};

static struct rtos_mutex s_mutex_pool[RTOS_MAX_MUTEXES];
static uint32_t s_mutex_used_mask = 0;

/* ============================================================
 * 🔧 静态池管理
 * ============================================================ */

static struct rtos_mutex *rtos_mutex_alloc(void)
{
    RTOS_ENTER_CRITICAL();
    for (int i = 0; i < RTOS_MAX_MUTEXES; i++) {
        if (!(s_mutex_used_mask & (1U << i))) {
            s_mutex_used_mask |= (1U << i);
            memset(&s_mutex_pool[i], 0, sizeof(s_mutex_pool[i]));
            rtos_list_init(&s_mutex_pool[i].wait_list);
            RTOS_EXIT_CRITICAL();
            return &s_mutex_pool[i];
        }
    }
    RTOS_EXIT_CRITICAL();
    return NULL;
}

static void rtos_mutex_free(struct rtos_mutex *m)
{
    int idx = (int)(m - s_mutex_pool);
    if (idx >= 0 && idx < RTOS_MAX_MUTEXES) {
        RTOS_ENTER_CRITICAL();
        s_mutex_used_mask &= ~(1U << idx);
        RTOS_EXIT_CRITICAL();
    }
}

/* ============================================================
 * 🔗 等待队列辅助
 * ============================================================ */

static void rtos_mutex_wait_list_insert(struct rtos_list_node *wait_list,
                                         struct rtos_tcb *tcb)
{
    struct rtos_list_node *pos;
    rtos_list_for_each(pos, wait_list) {
        struct rtos_tcb *p = rtos_list_entry(pos, struct rtos_tcb, ready_node);
        if (p->priority < tcb->priority) {
            break;
        }
    }
    rtos_list_insert_before(pos, &tcb->ready_node);
}

static struct rtos_tcb *rtos_mutex_wait_list_take_highest(struct rtos_list_node *wait_list)
{
    if (rtos_list_is_empty(wait_list)) {
        return NULL;
    }
    struct rtos_list_node *node = wait_list->next;
    rtos_list_remove(node);
    return rtos_list_entry(node, struct rtos_tcb, ready_node);
}

/* ============================================================
 * 👑 优先级继承
 * ============================================================ */

#if RTOS_ENABLE_PRIORITY_INHERITANCE

static void rtos_mutex_priority_inherit(struct rtos_mutex *m,
                                         struct rtos_tcb *waiter)
{
    if (!m->holder || m->holder->priority >= waiter->priority) {
        return;
    }
    m->holder->priority = waiter->priority;
    if (m->holder->state == RTOS_TASK_READY || m->holder->state == RTOS_TASK_RUNNING) {
        rtos_task_unready(m->holder);
        rtos_task_ready(m->holder);
    }
}

static void rtos_mutex_priority_disinherit(struct rtos_mutex *m)
{
    if (!m->holder) {
        return;
    }
    uint32_t highest = m->holder->base_priority;
    struct rtos_list_node *pos;
    rtos_list_for_each(pos, &m->wait_list) {
        struct rtos_tcb *tcb = rtos_list_entry(pos, struct rtos_tcb, ready_node);
        if (tcb->priority > highest) {
            highest = tcb->priority;
        }
    }
    if (m->holder->priority != highest) {
        m->holder->priority = highest;
        if (m->holder->state == RTOS_TASK_READY || m->holder->state == RTOS_TASK_RUNNING) {
            rtos_task_unready(m->holder);
            rtos_task_ready(m->holder);
        }
    }
}

#endif /* RTOS_ENABLE_PRIORITY_INHERITANCE */

/* ============================================================
 * 📌 API 实现
 * ============================================================ */

rtos_err_t rtos_mutex_create(rtos_mutex_handle_t *mutex)
{
    rtos_kernel_init();
    if (!mutex) {
        return RTOS_ERR_PARAM;
    }
    struct rtos_mutex *m = rtos_mutex_alloc();
    if (!m) {
        return RTOS_ERR_NOMEM;
    }
    m->holder = NULL;
    m->recursion = 0;
    *mutex = m;
    return RTOS_OK;
}

void rtos_mutex_delete(rtos_mutex_handle_t mutex)
{
    struct rtos_mutex *m = (struct rtos_mutex *)mutex;
    if (!m) {
        return;
    }

    RTOS_ENTER_CRITICAL();
#if RTOS_ENABLE_PRIORITY_INHERITANCE
    if (m->holder) {
        m->holder->priority = m->holder->base_priority;
        if (m->holder->state == RTOS_TASK_READY || m->holder->state == RTOS_TASK_RUNNING) {
            rtos_task_unready(m->holder);
            rtos_task_ready(m->holder);
        }
    }
#endif
    struct rtos_tcb *tcb;
    while ((tcb = rtos_mutex_wait_list_take_highest(&m->wait_list)) != NULL) {
        if (tcb->wake_tick != 0) {
            rtos_list_remove(&tcb->delay_node);
            tcb->wake_tick = 0;
        }
        tcb->blocking_obj = NULL;
        tcb->block_result = RTOS_ERR_ABORTED;
        rtos_task_ready(tcb);
    }
    RTOS_EXIT_CRITICAL();
    rtos_mutex_free(m);
}

rtos_err_t rtos_mutex_take(rtos_mutex_handle_t mutex, uint32_t timeout_ticks)
{
    struct rtos_mutex *m = (struct rtos_mutex *)mutex;
    if (!m) {
        return RTOS_ERR_PARAM;
    }

    RTOS_ENTER_CRITICAL();
    if (!m->holder) {
        m->holder = g_kernel.current_task;
        m->recursion = 1;
        RTOS_EXIT_CRITICAL();
        return RTOS_OK;
    }

    if (m->holder == g_kernel.current_task) {
        m->recursion++;
        RTOS_EXIT_CRITICAL();
        return RTOS_OK;
    }

    if (timeout_ticks == RTOS_DONT_WAIT) {
        RTOS_EXIT_CRITICAL();
        return RTOS_ERR_TIMEOUT;
    }

    struct rtos_tcb *tcb = g_kernel.current_task;
    tcb->blocking_obj = m;
    tcb->block_result = RTOS_OK;
    rtos_task_unready(tcb);
    rtos_mutex_wait_list_insert(&m->wait_list, tcb);

#if RTOS_ENABLE_PRIORITY_INHERITANCE
    rtos_mutex_priority_inherit(m, tcb);
#endif

    if (timeout_ticks != RTOS_WAIT_FOREVER) {
        tcb->wake_tick = g_kernel.tick_count + timeout_ticks;
        struct rtos_list_node *pos;
        rtos_list_for_each(pos, &g_kernel.delay_list) {
            struct rtos_tcb *p = rtos_list_entry(pos, struct rtos_tcb, delay_node);
            if ((int32_t)(p->wake_tick - tcb->wake_tick) > 0) {
                break;
            }
        }
        rtos_list_insert_before(pos, &tcb->delay_node);
    } else {
        tcb->wake_tick = 0;
    }

    RTOS_EXIT_CRITICAL();
    rtos_sched();

    return tcb->block_result;
}

rtos_err_t rtos_mutex_give(rtos_mutex_handle_t mutex)
{
    struct rtos_mutex *m = (struct rtos_mutex *)mutex;
    if (!m) {
        return RTOS_ERR_PARAM;
    }

    RTOS_ENTER_CRITICAL();
    if (m->holder != g_kernel.current_task) {
        RTOS_EXIT_CRITICAL();
        return RTOS_ERR_FAIL;
    }

    if (m->recursion > 1) {
        m->recursion--;
        RTOS_EXIT_CRITICAL();
        return RTOS_OK;
    }

    m->recursion = 0;
    m->holder = NULL;

#if RTOS_ENABLE_PRIORITY_INHERITANCE
    struct rtos_tcb *curr = g_kernel.current_task;
    if (curr->priority != curr->base_priority) {
        curr->priority = curr->base_priority;
        rtos_task_unready(curr);
        rtos_task_ready(curr);
    }
#endif

    struct rtos_tcb *tcb = rtos_mutex_wait_list_take_highest(&m->wait_list);
    if (tcb) {
        if (tcb->wake_tick != 0) {
            rtos_list_remove(&tcb->delay_node);
            tcb->wake_tick = 0;
        }
        tcb->blocking_obj = NULL;
        tcb->block_result = RTOS_OK;
        m->holder = tcb;
        m->recursion = 1;
        rtos_task_ready(tcb);
        RTOS_EXIT_CRITICAL();
        rtos_sched();
        return RTOS_OK;
    }

    RTOS_EXIT_CRITICAL();
    return RTOS_OK;
}

rtos_task_handle_t rtos_mutex_get_holder(rtos_mutex_handle_t mutex)
{
    struct rtos_mutex *m = (struct rtos_mutex *)mutex;
    if (!m) {
        return NULL;
    }
    return m->holder;
}
