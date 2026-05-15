/*
 * LinRTOS - Software timer (callback directly from tick ISR).
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#include <string.h>
#include "rtos.h"
#include "rtos_kernel.h"
#include "rtos_port.h"

#if RTOS_ENABLE_SOFT_TIMER

#define RTOS_MAX_TIMERS     16

struct rtos_timer {
    struct rtos_list_node node;
    uint32_t period;
    uint32_t expire_tick;
    rtos_timer_mode_t mode;
    rtos_timer_callback_t callback;
    void *arg;
    uint8_t is_active;
    char name[RTOS_MAX_TASK_NAME_LEN];
};

static struct rtos_timer s_timer_pool[RTOS_MAX_TIMERS];
static uint32_t s_timer_used_mask = 0;

/* ============================================================
 * 🔧 静态池
 * ============================================================ */

static struct rtos_timer *rtos_timer_alloc(void)
{
    RTOS_ENTER_CRITICAL();
    for (int i = 0; i < RTOS_MAX_TIMERS; i++) {
        if (!(s_timer_used_mask & (1U << i))) {
            s_timer_used_mask |= (1U << i);
            memset(&s_timer_pool[i], 0, sizeof(s_timer_pool[i]));
            rtos_list_init(&s_timer_pool[i].node);
            RTOS_EXIT_CRITICAL();
            return &s_timer_pool[i];
        }
    }
    RTOS_EXIT_CRITICAL();
    return NULL;
}

static void rtos_timer_free(struct rtos_timer *timer)
{
    int idx = (int)(timer - s_timer_pool);
    if (idx >= 0 && idx < RTOS_MAX_TIMERS) {
        RTOS_ENTER_CRITICAL();
        s_timer_used_mask &= ~(1U << idx);
        RTOS_EXIT_CRITICAL();
    }
}

/* ============================================================
 * ⏱️ Tick 处理（由 rtos_tick_handler 调用）
 * ============================================================ */

void rtos_timer_tick_handler(void)
{
    struct rtos_list_node *pos, *n;
    rtos_list_for_each_safe(pos, n, &g_kernel.timer_list) {
        struct rtos_timer *timer = rtos_list_entry(pos, struct rtos_timer, node);
        if (!timer->is_active) {
            continue;
        }
        if ((int32_t)(g_kernel.tick_count - timer->expire_tick) >= 0) {
            if (timer->mode == RTOS_TIMER_ONE_SHOT) {
                timer->is_active = 0;
                rtos_list_remove(&timer->node);
            } else {
                timer->expire_tick += timer->period;
            }
            if (timer->callback) {
                timer->callback((rtos_timer_handle_t)timer);
            }
        } else {
            /* timer_list 按 expire_tick 升序，后面的更晚 */
            break;
        }
    }
}

/* ============================================================
 * 📌 API 实现
 * ============================================================ */

rtos_err_t rtos_timer_create(rtos_timer_handle_t *timer,
                              const char *name,
                              uint32_t period_ticks,
                              rtos_timer_mode_t mode,
                              void *arg,
                              rtos_timer_callback_t callback)
{
    rtos_kernel_init();
    if (!timer || !period_ticks || !callback) {
        return RTOS_ERR_PARAM;
    }
    struct rtos_timer *tm = rtos_timer_alloc();
    if (!tm) {
        return RTOS_ERR_NOMEM;
    }

    strncpy(tm->name, name ? name : "", RTOS_MAX_TASK_NAME_LEN - 1);
    tm->name[RTOS_MAX_TASK_NAME_LEN - 1] = '\0';
    tm->period = period_ticks;
    tm->expire_tick = 0;
    tm->mode = mode;
    tm->callback = callback;
    tm->arg = arg;
    tm->is_active = 0;

    *timer = tm;
    return RTOS_OK;
}

void rtos_timer_delete(rtos_timer_handle_t timer)
{
    struct rtos_timer *tm = (struct rtos_timer *)timer;
    if (!tm) {
        return;
    }
    RTOS_ENTER_CRITICAL();
    if (tm->is_active) {
        rtos_list_remove(&tm->node);
    }
    RTOS_EXIT_CRITICAL();
    rtos_timer_free(tm);
}

rtos_err_t rtos_timer_start(rtos_timer_handle_t timer)
{
    struct rtos_timer *tm = (struct rtos_timer *)timer;
    if (!tm) {
        return RTOS_ERR_PARAM;
    }

    RTOS_ENTER_CRITICAL();
    if (tm->is_active) {
        rtos_list_remove(&tm->node);
    }
    tm->is_active = 1;
    tm->expire_tick = g_kernel.tick_count + tm->period;

    /* 按 expire_tick 升序插入 */
    struct rtos_list_node *pos;
    rtos_list_for_each(pos, &g_kernel.timer_list) {
        struct rtos_timer *p = rtos_list_entry(pos, struct rtos_timer, node);
        if ((int32_t)(p->expire_tick - tm->expire_tick) > 0) {
            break;
        }
    }
    rtos_list_insert_before(pos, &tm->node);
    RTOS_EXIT_CRITICAL();

    return RTOS_OK;
}

rtos_err_t rtos_timer_stop(rtos_timer_handle_t timer)
{
    struct rtos_timer *tm = (struct rtos_timer *)timer;
    if (!tm) {
        return RTOS_ERR_PARAM;
    }
    RTOS_ENTER_CRITICAL();
    if (tm->is_active) {
        tm->is_active = 0;
        rtos_list_remove(&tm->node);
    }
    RTOS_EXIT_CRITICAL();
    return RTOS_OK;
}

rtos_err_t rtos_timer_reset(rtos_timer_handle_t timer)
{
    return rtos_timer_start(timer);
}

rtos_err_t rtos_timer_change_period(rtos_timer_handle_t timer,
                                     uint32_t new_period_ticks)
{
    struct rtos_timer *tm = (struct rtos_timer *)timer;
    if (!tm || !new_period_ticks) {
        return RTOS_ERR_PARAM;
    }
    RTOS_ENTER_CRITICAL();
    tm->period = new_period_ticks;
    RTOS_EXIT_CRITICAL();
    return rtos_timer_start(timer);
}

#endif /* RTOS_ENABLE_SOFT_TIMER */
