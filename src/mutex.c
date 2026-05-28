/*
 * LinRTOS - Mutex & Recursive Mutex implementation.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 *
 * 互斥锁基于统一队列实现，附加：
 *   - 所有权：mutex_holder 记录当前持有者
 *   - 优先级继承：高优先级任务阻塞时临时提升持有者优先级
 *   - 递归：同一线程可多次获取，计数归零后才真正释放
 */

#include "rtos_mutex.h"
#include "linRTOS.h"
#include "port.h"

/* 引用 queue.c 中的内部辅助函数 */
extern struct rtos_tcb *prv_wake_highest_from_event_list(
    struct rtos_list_node *event_list);
extern void prv_block_current_task(struct rtos_list_node *event_list,
                                   uint32_t timeout);

/* ============================================================
 * 🏗️ 生命周期
 * ============================================================ */

rtos_err_t rtos_mutex_init(struct rtos_queue *mutex)
{
    if (!mutex) {
        return RTOS_ERR_PARAM;
    }
    rtos_err_t err = rtos_queue_init(mutex, NULL, 1, 0);
    if (err != RTOS_OK) {
        return err;
    }
    mutex->queue_type = RTOS_QUEUE_TYPE_MUTEX;
    /* 互斥锁创建后处于"可用"状态（与信号量相反） */
    mutex->messages_waiting = 1;
    mutex->mutex_holder = NULL;
    mutex->recursive_count = 0;
    mutex->original_priority = 0xFFFFFFFF;
    return RTOS_OK;
}

rtos_err_t rtos_mutex_init_recursive(struct rtos_queue *mutex)
{
    if (!mutex) {
        return RTOS_ERR_PARAM;
    }
    rtos_err_t err = rtos_queue_init(mutex, NULL, 1, 0);
    if (err != RTOS_OK) {
        return err;
    }
    mutex->queue_type = RTOS_QUEUE_TYPE_RECURSIVE;
    mutex->messages_waiting = 1;
    mutex->mutex_holder = NULL;
    mutex->recursive_count = 0;
    mutex->original_priority = 0xFFFFFFFF;
    return RTOS_OK;
}

void rtos_mutex_delete(struct rtos_queue *mutex)
{
    if (mutex) {
        rtos_queue_delete(mutex);
    }
}

/* ============================================================
 * 🔐 普通互斥锁
 * ============================================================ */

rtos_err_t rtos_mutex_take(struct rtos_queue *mutex, uint32_t timeout)
{
    RTOS_ASSERT(mutex);
    RTOS_ASSERT(mutex->queue_type == RTOS_QUEUE_TYPE_MUTEX ||
                mutex->queue_type == RTOS_QUEUE_TYPE_RECURSIVE);

    struct rtos_tcb *curr = (struct rtos_tcb *)rtos_current_tcb;

    RTOS_ENTER_CRITICAL();

    /* 互斥锁可用 */
    if (mutex->messages_waiting > 0) {
        mutex->messages_waiting = 0;
        mutex->mutex_holder = curr;
        mutex->original_priority = 0xFFFFFFFF;
        curr->held_queue = mutex;
        RTOS_EXIT_CRITICAL();
        return RTOS_OK;
    }

    /* 不可用时检查是否允许阻塞 */
    if (timeout == RTOS_DONT_WAIT) {
        RTOS_EXIT_CRITICAL();
        return RTOS_ERR_RESOURCE;
    }

    /* 优先级继承：如果当前任务优先级高于持有者，提升持有者 */
    if (mutex->mutex_holder &&
        curr->priority > mutex->mutex_holder->priority) {
        if (mutex->original_priority == 0xFFFFFFFF) {
            mutex->original_priority = mutex->mutex_holder->priority;
        }
        rtos_task_set_priority(mutex->mutex_holder, curr->priority);
    }

    /* 阻塞到接收等待链表 */
    prv_block_current_task(&mutex->tasks_waiting_to_receive, timeout);
    RTOS_EXIT_CRITICAL();
    rtos_sched();

    /* 被唤醒后 */
    if (curr->wakeup_reason == 2) {
        curr->wakeup_reason = 0;
        return RTOS_ERR_TIMEOUT;
    }
    curr->wakeup_reason = 0;

    /* 正常唤醒后，give 方已将 holder 设为当前任务 */
    RTOS_ASSERT(mutex->mutex_holder == curr);
    return RTOS_OK;
}

rtos_err_t rtos_mutex_give(struct rtos_queue *mutex)
{
    RTOS_ASSERT(mutex);
    RTOS_ASSERT(mutex->queue_type == RTOS_QUEUE_TYPE_MUTEX ||
                mutex->queue_type == RTOS_QUEUE_TYPE_RECURSIVE);

    struct rtos_tcb *curr = (struct rtos_tcb *)rtos_current_tcb;

    RTOS_ENTER_CRITICAL();

    /* 只有持有者才能释放 */
    if (mutex->mutex_holder != curr) {
        RTOS_EXIT_CRITICAL();
        return RTOS_ERR_STATE;
    }

    /* 恢复原始优先级（若曾被提升） */
    if (mutex->original_priority != 0xFFFFFFFF) {
        rtos_task_set_priority(curr, mutex->original_priority);
        mutex->original_priority = 0xFFFFFFFF;
    }

    /* 唤醒等待链表中最高优先级的任务 */
    struct rtos_tcb *woken =
        prv_wake_highest_from_event_list(&mutex->tasks_waiting_to_receive);

    curr->held_queue = NULL;

    if (woken) {
        mutex->mutex_holder = woken;
        mutex->original_priority = 0xFFFFFFFF;
        woken->held_queue = mutex;
    } else {
        mutex->messages_waiting = 1;
        mutex->mutex_holder = NULL;
    }

    RTOS_EXIT_CRITICAL();

    if (woken && woken->priority > curr->priority) {
        rtos_sched();
    }

    return RTOS_OK;
}

/* ============================================================
 * 🔐 递归互斥锁
 * ============================================================ */

rtos_err_t rtos_mutex_take_recursive(struct rtos_queue *mutex,
                                     uint32_t timeout)
{
    RTOS_ASSERT(mutex);
    RTOS_ASSERT(mutex->queue_type == RTOS_QUEUE_TYPE_RECURSIVE);

    struct rtos_tcb *curr = (struct rtos_tcb *)rtos_current_tcb;

    RTOS_ENTER_CRITICAL();

    /* 若当前任务已是持有者，递归计数+1 */
    if (mutex->mutex_holder == curr) {
        mutex->recursive_count++;
        RTOS_EXIT_CRITICAL();
        return RTOS_OK;
    }

    RTOS_EXIT_CRITICAL();

    /* 否则走普通 take 路径 */
    rtos_err_t err = rtos_mutex_take(mutex, timeout);
    if (err == RTOS_OK) {
        RTOS_ENTER_CRITICAL();
        mutex->recursive_count = 1;
        RTOS_EXIT_CRITICAL();
    }
    return err;
}

rtos_err_t rtos_mutex_give_recursive(struct rtos_queue *mutex)
{
    RTOS_ASSERT(mutex);
    RTOS_ASSERT(mutex->queue_type == RTOS_QUEUE_TYPE_RECURSIVE);

    struct rtos_tcb *curr = (struct rtos_tcb *)rtos_current_tcb;

    RTOS_ENTER_CRITICAL();

    /* 只有持有者才能释放 */
    if (mutex->mutex_holder != curr) {
        RTOS_EXIT_CRITICAL();
        return RTOS_ERR_STATE;
    }

    /* 递归计数减 1，只有归零时才真正释放 */
    mutex->recursive_count--;
    if (mutex->recursive_count > 0) {
        RTOS_EXIT_CRITICAL();
        return RTOS_OK;
    }

    RTOS_EXIT_CRITICAL();

    return rtos_mutex_give(mutex);
}
