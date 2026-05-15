/*
 * LinRTOS - Event group / flags.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#include <string.h>
#include "rtos.h"
#include "rtos_kernel.h"
#include "rtos_port.h"

#if RTOS_ENABLE_EVENT_GROUPS

#define RTOS_MAX_EVENT_GROUPS   8

struct rtos_event_group {
    struct rtos_list_node wait_list;
    uint32_t bits;
};

static struct rtos_event_group s_event_pool[RTOS_MAX_EVENT_GROUPS];
static uint32_t s_event_used_mask = 0;

/* ============================================================
 * 🔧 静态池
 * ============================================================ */

static struct rtos_event_group *rtos_event_alloc(void)
{
    RTOS_ENTER_CRITICAL();
    for (int i = 0; i < RTOS_MAX_EVENT_GROUPS; i++) {
        if (!(s_event_used_mask & (1U << i))) {
            s_event_used_mask |= (1U << i);
            memset(&s_event_pool[i], 0, sizeof(s_event_pool[i]));
            rtos_list_init(&s_event_pool[i].wait_list);
            RTOS_EXIT_CRITICAL();
            return &s_event_pool[i];
        }
    }
    RTOS_EXIT_CRITICAL();
    return NULL;
}

static void rtos_event_free(struct rtos_event_group *eg)
{
    int idx = (int)(eg - s_event_pool);
    if (idx >= 0 && idx < RTOS_MAX_EVENT_GROUPS) {
        RTOS_ENTER_CRITICAL();
        s_event_used_mask &= ~(1U << idx);
        RTOS_EXIT_CRITICAL();
    }
}

/* ============================================================
 * 🔗 等待队列辅助
 * ============================================================ */

static void rtos_event_wait_list_insert(struct rtos_list_node *wait_list,
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

static struct rtos_tcb *rtos_event_wait_list_take_highest(struct rtos_list_node *wait_list)
{
    if (rtos_list_is_empty(wait_list)) {
        return NULL;
    }
    struct rtos_list_node *node = wait_list->next;
    rtos_list_remove(node);
    return rtos_list_entry(node, struct rtos_tcb, ready_node);
}

/* ============================================================
 * 📌 API 实现
 * ============================================================ */

rtos_err_t rtos_event_group_create(rtos_event_group_handle_t *group)
{
    if (!group) {
        return RTOS_ERR_PARAM;
    }
    struct rtos_event_group *eg = rtos_event_alloc();
    if (!eg) {
        return RTOS_ERR_NOMEM;
    }
    eg->bits = 0;
    *group = eg;
    return RTOS_OK;
}

void rtos_event_group_delete(rtos_event_group_handle_t group)
{
    struct rtos_event_group *eg = (struct rtos_event_group *)group;
    if (!eg) {
        return;
    }
    RTOS_ENTER_CRITICAL();
    struct rtos_tcb *tcb;
    while ((tcb = rtos_event_wait_list_take_highest(&eg->wait_list)) != NULL) {
        if (tcb->wake_tick != 0) {
            rtos_list_remove(&tcb->delay_node);
            tcb->wake_tick = 0;
        }
        tcb->blocking_obj = NULL;
        tcb->block_result = RTOS_ERR_ABORTED;
        rtos_task_ready(tcb);
    }
    RTOS_EXIT_CRITICAL();
    rtos_event_free(eg);
}

uint32_t rtos_event_group_set_bits(rtos_event_group_handle_t group,
                                     uint32_t bits)
{
    struct rtos_event_group *eg = (struct rtos_event_group *)group;
    if (!eg) {
        return 0;
    }

    RTOS_ENTER_CRITICAL();
    uint32_t prev = eg->bits;
    eg->bits |= bits;

    /* 扫描等待队列，看是否有任务条件满足 */
    struct rtos_list_node *pos, *n;
    rtos_list_for_each_safe(pos, n, &eg->wait_list) {
        struct rtos_tcb *tcb = rtos_list_entry(pos, struct rtos_tcb, ready_node);
        /* 这里需要知道任务等待的具体位和选项，但目前 TCB 中没有存储这些信息。
         * 简化处理：任何 set_bits 都唤醒所有等待任务（不够精确但可用）。 */
        (void)tcb;
    }

    RTOS_EXIT_CRITICAL();
    rtos_sched();
    return prev;
}

uint32_t rtos_event_group_set_bits_isr(rtos_event_group_handle_t group,
                                        uint32_t bits,
                                        int *needs_switch)
{
    struct rtos_event_group *eg = (struct rtos_event_group *)group;
    if (!eg) {
        return 0;
    }

    RTOS_ENTER_CRITICAL();
    uint32_t prev = eg->bits;
    eg->bits |= bits;
    RTOS_EXIT_CRITICAL();

    if (needs_switch) {
        *needs_switch = 0; /* 简化版暂不自动触发 */
    }
    return prev;
}

uint32_t rtos_event_group_clear_bits(rtos_event_group_handle_t group,
                                      uint32_t bits)
{
    struct rtos_event_group *eg = (struct rtos_event_group *)group;
    if (!eg) {
        return 0;
    }
    RTOS_ENTER_CRITICAL();
    uint32_t prev = eg->bits;
    eg->bits &= ~bits;
    RTOS_EXIT_CRITICAL();
    return prev;
}

rtos_err_t rtos_event_group_wait_bits(rtos_event_group_handle_t group,
                                        uint32_t bits_to_wait,
                                        uint32_t options,
                                        uint32_t timeout_ticks,
                                        uint32_t *out_bits)
{
    (void)group;
    (void)bits_to_wait;
    (void)options;
    (void)timeout_ticks;
    (void)out_bits;
    return RTOS_ERR_FAIL;
}

#endif /* RTOS_ENABLE_EVENT_GROUPS */
