/*
 * LinRTOS - Semaphore API.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 *
 * 信号量是统一队列的语法糖包装：
 *   - 二进制信号量：length=1, item_size=0, messages_waiting 初始为 0
 *   - 计数信号量：length=max_count, item_size=0, messages_waiting 初始为 initial
 *
 * give = send（不阻塞），take = recv（可阻塞）。
 */

#ifndef RTOS_SEMAPHORE_H
#define RTOS_SEMAPHORE_H

#include "rtos_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 📌 信号量句柄（本质就是队列指针）
 * ============================================================ */

typedef struct rtos_queue *rtos_semaphore_handle_t;

/* ============================================================
 * 🏗️ 生命周期（静态初始化，零堆依赖）
 * ============================================================ */

/**
 * @brief 初始化二进制信号量
 * @param sem 用户静态分配的队列结构体
 * @return RTOS_OK / RTOS_ERR_PARAM
 *
 * @note 创建后信号量处于"空"状态（count=0），需要 give 后才能 take。
 */
rtos_err_t rtos_semaphore_init_binary(struct rtos_queue *sem);

/**
 * @brief 初始化计数信号量
 * @param sem        用户静态分配的队列结构体
 * @param max_count  最大计数值
 * @param initial    初始计数值（必须 <= max_count）
 * @return RTOS_OK / RTOS_ERR_PARAM
 */
rtos_err_t rtos_semaphore_init_counting(struct rtos_queue *sem,
                                        uint32_t max_count,
                                        uint32_t initial);

/**
 * @brief 删除信号量
 * @note 用户负责释放 sem 结构体内存。删除前必须无任务阻塞。
 */
void rtos_semaphore_delete(struct rtos_queue *sem);

/* ============================================================
 * 📨 任务上下文操作
 * ============================================================ */

/**
 * @brief 释放信号量（V 操作）
 * @return RTOS_OK / RTOS_ERR_RESOURCE（计数已达上限）
 */
rtos_err_t rtos_semaphore_give(struct rtos_queue *sem);

/**
 * @brief 获取信号量（P 操作，可阻塞）
 * @param timeout RTOS_DONT_WAIT / RTOS_WAIT_FOREVER / ticks
 * @return RTOS_OK / RTOS_ERR_RESOURCE / RTOS_ERR_TIMEOUT
 */
rtos_err_t rtos_semaphore_take(struct rtos_queue *sem, uint32_t timeout);

/* ============================================================
 * ⚡ ISR 安全版本
 * ============================================================ */

rtos_err_t rtos_semaphore_give_from_isr(struct rtos_queue *sem,
                                        bool *pxHigherPrioTaskWoken);
rtos_err_t rtos_semaphore_take_from_isr(struct rtos_queue *sem,
                                        bool *pxHigherPrioTaskWoken);

/* ============================================================
 * 📊 状态查询
 * ============================================================ */

static inline uint32_t rtos_semaphore_get_count(struct rtos_queue *sem)
{
    return sem ? sem->messages_waiting : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RTOS_SEMAPHORE_H */
