/*
 * LinRTOS - Semaphore implementation.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 *
 * 信号量是统一队列的零成本包装：
 *   - 二进制/计数信号量底层都是 item_size==0 的队列
 *   - give = send, take = recv
 *   - 不搬数据，只操作 messages_waiting 计数
 */

#include "rtos_semaphore.h"

/* ============================================================
 * 🏗️ 生命周期
 * ============================================================ */

rtos_err_t rtos_semaphore_init_binary(struct rtos_queue *sem)
{
    if (!sem) {
        return RTOS_ERR_PARAM;
    }
    rtos_err_t err = rtos_queue_init(sem, NULL, 1, 0);
    if (err != RTOS_OK) {
        return err;
    }
    sem->queue_type = RTOS_QUEUE_TYPE_BINARY;
    /* 二进制信号量创建后 count=0（空），需要 give 后才能 take */
    sem->messages_waiting = 0;
    return RTOS_OK;
}

rtos_err_t rtos_semaphore_init_counting(struct rtos_queue *sem,
                                        uint32_t max_count,
                                        uint32_t initial)
{
    if (!sem || max_count == 0 || initial > max_count) {
        return RTOS_ERR_PARAM;
    }
    rtos_err_t err = rtos_queue_init(sem, NULL, max_count, 0);
    if (err != RTOS_OK) {
        return err;
    }
    sem->queue_type = RTOS_QUEUE_TYPE_COUNTING;
    sem->messages_waiting = initial;
    return RTOS_OK;
}

void rtos_semaphore_delete(struct rtos_queue *sem)
{
    if (sem) {
        rtos_queue_delete(sem);
    }
}

/* ============================================================
 * 📨 任务上下文
 * ============================================================ */

rtos_err_t rtos_semaphore_give(struct rtos_queue *sem)
{
    return rtos_queue_generic_send(sem, NULL, RTOS_DONT_WAIT,
                                   RTOS_QUEUE_SEND_BACK);
}

rtos_err_t rtos_semaphore_take(struct rtos_queue *sem, uint32_t timeout)
{
    return rtos_queue_generic_recv(sem, NULL, timeout, false);
}

/* ============================================================
 * ⚡ ISR 安全版本
 * ============================================================ */

rtos_err_t rtos_semaphore_give_from_isr(struct rtos_queue *sem,
                                        bool *pxHigherPrioTaskWoken)
{
    return rtos_queue_generic_send_from_isr(sem, NULL,
                                            RTOS_QUEUE_SEND_BACK,
                                            pxHigherPrioTaskWoken);
}

rtos_err_t rtos_semaphore_take_from_isr(struct rtos_queue *sem,
                                        bool *pxHigherPrioTaskWoken)
{
    return rtos_queue_generic_recv_from_isr(sem, NULL,
                                            pxHigherPrioTaskWoken);
}
