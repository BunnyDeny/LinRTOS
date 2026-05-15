#include <string.h>
#include "rtos.h"
#include "rtos_kernel.h"
#include "rtos_port.h"

#define RTOS_MAX_SEMAPHORES     16

struct rtos_sem {
    struct rtos_list_node wait_list;
    uint32_t count;
};

static struct rtos_sem s_sem_pool[RTOS_MAX_SEMAPHORES];
static uint32_t s_sem_used_mask = 0;

static struct rtos_sem *rtos_sem_alloc(void)
{
    RTOS_ENTER_CRITICAL();
    for (int i = 0; i < RTOS_MAX_SEMAPHORES; i++) {
        if (!(s_sem_used_mask & (1U << i))) {
            s_sem_used_mask |= (1U << i);
            memset(&s_sem_pool[i], 0, sizeof(s_sem_pool[i]));
            rtos_list_init(&s_sem_pool[i].wait_list);
            RTOS_EXIT_CRITICAL();
            return &s_sem_pool[i];
        }
    }
    RTOS_EXIT_CRITICAL();
    return NULL;
}

static void rtos_sem_free(struct rtos_sem *sem)
{
    int idx = (int)(sem - s_sem_pool);
    if (idx >= 0 && idx < RTOS_MAX_SEMAPHORES) {
        RTOS_ENTER_CRITICAL();
        s_sem_used_mask &= ~(1U << idx);
        RTOS_EXIT_CRITICAL();
    }
}

static void rtos_wait_list_insert(struct rtos_list_node *wait_list,
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

static struct rtos_tcb *rtos_wait_list_take_highest(struct rtos_list_node *wait_list)
{
    if (rtos_list_is_empty(wait_list)) {
        return NULL;
    }
    struct rtos_list_node *node = wait_list->next;
    rtos_list_remove(node);
    return rtos_list_entry(node, struct rtos_tcb, ready_node);
}

rtos_err_t rtos_sem_create(rtos_sem_handle_t *sem, uint32_t initial_count)
{
    rtos_kernel_init();
    if (!sem) {
        return RTOS_ERR_PARAM;
    }
    struct rtos_sem *s = rtos_sem_alloc();
    if (!s) {
        return RTOS_ERR_NOMEM;
    }
    s->count = initial_count;
    *sem = s;
    return RTOS_OK;
}

void rtos_sem_delete(rtos_sem_handle_t sem)
{
    struct rtos_sem *s = (struct rtos_sem *)sem;
    if (!s) {
        return;
    }
    RTOS_ENTER_CRITICAL();
    struct rtos_tcb *tcb;
    while ((tcb = rtos_wait_list_take_highest(&s->wait_list)) != NULL) {
        if (tcb->wake_tick != 0) {
            rtos_list_remove(&tcb->delay_node);
            tcb->wake_tick = 0;
        }
        tcb->blocking_obj = NULL;
        tcb->block_result = RTOS_ERR_ABORTED;
        rtos_task_ready(tcb);
    }
    RTOS_EXIT_CRITICAL();
    rtos_sem_free(s);
}

rtos_err_t rtos_sem_take(rtos_sem_handle_t sem, uint32_t timeout_ticks)
{
    struct rtos_sem *s = (struct rtos_sem *)sem;
    if (!s) {
        return RTOS_ERR_PARAM;
    }

    RTOS_ENTER_CRITICAL();
    if (s->count > 0) {
        s->count--;
        RTOS_EXIT_CRITICAL();
        return RTOS_OK;
    }

    if (timeout_ticks == RTOS_DONT_WAIT) {
        RTOS_EXIT_CRITICAL();
        return RTOS_ERR_TIMEOUT;
    }

    struct rtos_tcb *tcb = g_kernel.current_task;
    tcb->blocking_obj = s;
    tcb->block_result = RTOS_OK;
    rtos_task_unready(tcb);
    rtos_wait_list_insert(&s->wait_list, tcb);

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

rtos_err_t rtos_sem_give(rtos_sem_handle_t sem)
{
    struct rtos_sem *s = (struct rtos_sem *)sem;
    if (!s) {
        return RTOS_ERR_PARAM;
    }

    RTOS_ENTER_CRITICAL();
    if (!rtos_list_is_empty(&s->wait_list)) {
        struct rtos_tcb *tcb = rtos_wait_list_take_highest(&s->wait_list);
        if (tcb->wake_tick != 0) {
            rtos_list_remove(&tcb->delay_node);
            tcb->wake_tick = 0;
        }
        tcb->blocking_obj = NULL;
        tcb->block_result = RTOS_OK;
        rtos_task_ready(tcb);
    } else {
        s->count++;
    }
    RTOS_EXIT_CRITICAL();

    if (g_kernel.is_running && !g_kernel.sched_lock) {
        rtos_sched();
    }

    return RTOS_OK;
}

rtos_err_t rtos_sem_give_isr(rtos_sem_handle_t sem, int *need_switch)
{
    struct rtos_sem *s = (struct rtos_sem *)sem;
    if (!s) {
        return RTOS_ERR_PARAM;
    }

    RTOS_ENTER_CRITICAL();
    if (!rtos_list_is_empty(&s->wait_list)) {
        struct rtos_tcb *tcb = rtos_wait_list_take_highest(&s->wait_list);
        if (tcb->wake_tick != 0) {
            rtos_list_remove(&tcb->delay_node);
            tcb->wake_tick = 0;
        }
        tcb->blocking_obj = NULL;
        tcb->block_result = RTOS_OK;
        rtos_task_ready(tcb);
    } else {
        s->count++;
    }
    RTOS_EXIT_CRITICAL();

    if (need_switch) {
        *need_switch = (g_kernel.is_running && !g_kernel.sched_lock);
    }

    return RTOS_OK;
}
