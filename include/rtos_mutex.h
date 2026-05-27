/*
 * LinRTOS - Mutex & Recursive Mutex API.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 *
 * 互斥锁是特殊的二进制信号量，附加：
 *   - 所有权：只有持有者才能释放
 *   - 优先级继承：防止优先级反转
 *   - 递归互斥锁：同一线程可多次获取
 */

#ifndef RTOS_MUTEX_H
#define RTOS_MUTEX_H

#include "rtos_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 📌 互斥锁句柄（本质就是队列指针）
 * ============================================================ */

typedef struct rtos_queue *rtos_mutex_handle_t;

/* ============================================================
 * 🏗️ 生命周期（静态初始化，零堆依赖）
 * ============================================================ */

/**
 * @brief 初始化普通互斥锁
 * @param mutex 用户静态分配的队列结构体
 * @return RTOS_OK / RTOS_ERR_PARAM
 *
 * @note 创建后互斥锁处于"可用"状态（与信号量相反）。
 */
rtos_err_t rtos_mutex_init(struct rtos_queue *mutex);

/**
 * @brief 初始化递归互斥锁
 * @param mutex 用户静态分配的队列结构体
 * @return RTOS_OK / RTOS_ERR_PARAM
 */
rtos_err_t rtos_mutex_init_recursive(struct rtos_queue *mutex);

/**
 * @brief 删除互斥锁
 * @note 用户负责释放结构体内存。删除前必须无任务阻塞。
 */
void rtos_mutex_delete(struct rtos_queue *mutex);

/* ============================================================
 * 🔐 普通互斥锁
 * ============================================================ */

/**
 * @brief 获取互斥锁（可阻塞）
 * @param timeout RTOS_DONT_WAIT / RTOS_WAIT_FOREVER / ticks
 * @return RTOS_OK / RTOS_ERR_RESOURCE / RTOS_ERR_TIMEOUT
 *
 * @note 支持优先级继承：若高优先级任务阻塞在此互斥锁上，
 *       持有者的优先级会被临时提升。
 */
rtos_err_t rtos_mutex_take(struct rtos_queue *mutex, uint32_t timeout);

/**
 * @brief 释放互斥锁
 * @return RTOS_OK / RTOS_ERR_STATE（当前任务非持有者）
 *
 * @note 释放时会恢复持有者的原始优先级（若曾被提升）。
 */
rtos_err_t rtos_mutex_give(struct rtos_queue *mutex);

/* ============================================================
 * 🔐 递归互斥锁
 * ============================================================ */

/**
 * @brief 递归获取互斥锁
 * @return RTOS_OK / RTOS_ERR_RESOURCE / RTOS_ERR_TIMEOUT
 *
 * @note 若当前任务已是持有者，recursive_count++，不阻塞。
 */
rtos_err_t rtos_mutex_take_recursive(struct rtos_queue *mutex,
                                     uint32_t timeout);

/**
 * @brief 递归释放互斥锁
 * @return RTOS_OK / RTOS_ERR_STATE
 *
 * @note 只有 recursive_count 减到 0 时才真正释放互斥锁。
 */
rtos_err_t rtos_mutex_give_recursive(struct rtos_queue *mutex);

/* ============================================================
 * 📊 查询
 * ============================================================ */

static inline rtos_task_handle_t rtos_mutex_get_holder(struct rtos_queue *mutex)
{
    return (mutex && (mutex->queue_type == RTOS_QUEUE_TYPE_MUTEX ||
                      mutex->queue_type == RTOS_QUEUE_TYPE_RECURSIVE))
               ? (rtos_task_handle_t)mutex->mutex_holder
               : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* RTOS_MUTEX_H */
