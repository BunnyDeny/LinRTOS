/*
 * LinRTOS - Message queue.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#include <string.h>
#include "rtos.h"
#include "rtos_kernel.h"
#include "rtos_port.h"

#if RTOS_ENABLE_QUEUES

#define RTOS_MAX_QUEUES     8

struct rtos_queue {
    struct rtos_list_node send_wait_list;
    struct rtos_list_node recv_wait_list;
    uint8_t *buffer;
    uint32_t item_size;
    uint32_t capacity;
    uint32_t count;
    uint32_t head;
    uint32_t tail;
};

static struct rtos_queue s_queue_pool[RTOS_MAX_QUEUES];
static uint32_t s_queue_used_mask = 0;

/* ============================================================
 * 🔧 静态池
 * ============================================================ */

static struct rtos_queue *rtos_queue_alloc(void)
{
    RTOS_ENTER_CRITICAL();
    for (int i = 0; i < RTOS_MAX_QUEUES; i++) {
        if (!(s_queue_used_mask & (1U << i))) {
            s_queue_used_mask |= (1U << i);
            memset(&s_queue_pool[i], 0, sizeof(s_queue_pool[i]));
            rtos_list_init(&s_queue_pool[i].send_wait_list);
            rtos_list_init(&s_queue_pool[i].recv_wait_list);
            RTOS_EXIT_CRITICAL();
            return &s_queue_pool[i];
        }
    }
    RTOS_EXIT_CRITICAL();
    return NULL;
}

static void rtos_queue_free(struct rtos_queue *q)
{
    int idx = (int)(q - s_queue_pool);
    if (idx >= 0 && idx < RTOS_MAX_QUEUES) {
        RTOS_ENTER_CRITICAL();
        s_queue_used_mask &= ~(1U << idx);
        RTOS_EXIT_CRITICAL();
    }
}

/* ============================================================
 * 🔗 等待队列辅助
 * ============================================================ */

static void rtos_queue_wait_list_insert(struct rtos_list_node *wait_list,
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

static struct rtos_tcb *rtos_queue_wait_list_take_highest(struct rtos_list_node *wait_list)
{
    if (rtos_list_is_empty(wait_list)) {
        return NULL;
    }
    struct rtos_list_node *node = wait_list->next;
    rtos_list_remove(node);
    return rtos_list_entry(node, struct rtos_tcb, ready_node);
}

/* ============================================================
 * 📦 环形缓冲区操作
 * ============================================================ */

static inline void rtos_queue_push(struct rtos_queue *q, const void *item)
{
    memcpy(q->buffer + q->tail * q->item_size, item, q->item_size);
    q->tail++;
    if (q->tail >= q->capacity) {
        q->tail = 0;
    }
    q->count++;
}

static inline void rtos_queue_pop(struct rtos_queue *q, void *buffer)
{
    memcpy(buffer, q->buffer + q->head * q->item_size, q->item_size);
    q->head++;
    if (q->head >= q->capacity) {
        q->head = 0;
    }
    q->count--;
}

/* ============================================================
 * 📌 API 实现
 * ============================================================ */

rtos_err_t rtos_queue_create(rtos_queue_handle_t *queue,
                              uint32_t item_size_bytes,
                              uint32_t capacity)
{
    if (!queue || !item_size_bytes || !capacity) {
        return RTOS_ERR_PARAM;
    }
    struct rtos_queue *q = rtos_queue_alloc();
    if (!q) {
        return RTOS_ERR_NOMEM;
    }

    /* 使用静态分配策略：由用户提供存储区会更通用，
     * 但为了 API 简洁，这里内部使用一个最大通用缓冲区。
     * 实际产品建议改为用户提供 buffer 指针以精确控制 RAM。 */
    q->buffer = NULL; /* 占位，后续若需动态可扩展 */
    (void)q->buffer;

    /* 简化版本：不支持内部 buffer，需要用户提供静态 buffer */
    /* 这里返回错误，提示用户此版本需要外部 buffer 支持 */
    (void)item_size_bytes;
    (void)capacity;

    rtos_queue_free(q);
    return RTOS_ERR_NOMEM;
}

void rtos_queue_delete(rtos_queue_handle_t queue)
{
    struct rtos_queue *q = (struct rtos_queue *)queue;
    if (!q) {
        return;
    }
    RTOS_ENTER_CRITICAL();
    struct rtos_tcb *tcb;
    while ((tcb = rtos_queue_wait_list_take_highest(&q->send_wait_list)) != NULL) {
        if (tcb->wake_tick != 0) {
            rtos_list_remove(&tcb->delay_node);
            tcb->wake_tick = 0;
        }
        tcb->blocking_obj = NULL;
        tcb->block_result = RTOS_ERR_ABORTED;
        rtos_task_ready(tcb);
    }
    while ((tcb = rtos_queue_wait_list_take_highest(&q->recv_wait_list)) != NULL) {
        if (tcb->wake_tick != 0) {
            rtos_list_remove(&tcb->delay_node);
            tcb->wake_tick = 0;
        }
        tcb->blocking_obj = NULL;
        tcb->block_result = RTOS_ERR_ABORTED;
        rtos_task_ready(tcb);
    }
    RTOS_EXIT_CRITICAL();
    rtos_queue_free(q);
}

rtos_err_t rtos_queue_send(rtos_queue_handle_t queue,
                            const void *item,
                            uint32_t timeout_ticks)
{
    (void)queue;
    (void)item;
    (void)timeout_ticks;
    return RTOS_ERR_FAIL;
}

rtos_err_t rtos_queue_send_isr(rtos_queue_handle_t queue,
                                const void *item,
                                int *needs_switch)
{
    (void)queue;
    (void)item;
    (void)needs_switch;
    return RTOS_ERR_FAIL;
}

rtos_err_t rtos_queue_receive(rtos_queue_handle_t queue,
                               void *buffer,
                               uint32_t timeout_ticks)
{
    (void)queue;
    (void)buffer;
    (void)timeout_ticks;
    return RTOS_ERR_FAIL;
}

rtos_err_t rtos_queue_receive_isr(rtos_queue_handle_t queue,
                                   void *buffer,
                                   int *needs_switch)
{
    (void)queue;
    (void)buffer;
    (void)needs_switch;
    return RTOS_ERR_FAIL;
}

uint32_t rtos_queue_get_count(rtos_queue_handle_t queue)
{
    struct rtos_queue *q = (struct rtos_queue *)queue;
    if (!q) {
        return 0;
    }
    return q->count;
}

uint32_t rtos_queue_get_spaces(rtos_queue_handle_t queue)
{
    struct rtos_queue *q = (struct rtos_queue *)queue;
    if (!q) {
        return 0;
    }
    return q->capacity - q->count;
}

#endif /* RTOS_ENABLE_QUEUES */
