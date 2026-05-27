/*
 * LinRTOS - Unified Queue / IPC primitive.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_QUEUE_H
#define RTOS_QUEUE_H

#include "types.h"
#include "kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 📌 统一队列（Queue）—— LinRTOS 的底层 IPC 原语
 *
 * 这是所有 IPC 机制的单一实现载体：
 *   - 消息队列：length=N, item_size=sizeof(T)
 *   - 二进制信号量：length=1, item_size=0
 *   - 计数信号量：length=N, item_size=0
 *   - 互斥锁：length=1, item_size=0（外加 queue_type 标记）
 *   - 递归互斥锁：length=1, item_size=0（外加 queue_type 标记）
 *
 * 当 item_size == 0 时，队列退化为纯计数器：不搬数据，只增减
 * messages_waiting（即信号量的 count）。
 * ============================================================ */

/* ---- 发送位置 ---- */
typedef enum {
    RTOS_QUEUE_SEND_BACK = 0,   /* 尾部追加（常规 FIFO） */
    RTOS_QUEUE_SEND_FRONT,      /* 头部插入（高优先级消息插队） */
    RTOS_QUEUE_SEND_OVERWRITE,  /* 覆盖写入（仅当 length == 1 时合法） */
} rtos_queue_send_pos_t;

/* ---- 队列类型标记（用于区分语义，当前仅预留） ---- */
#define RTOS_QUEUE_TYPE_BASE        0   /* 普通消息队列 */
#define RTOS_QUEUE_TYPE_MUTEX       1   /* 互斥锁 */
#define RTOS_QUEUE_TYPE_COUNTING    2   /* 计数信号量 */
#define RTOS_QUEUE_TYPE_BINARY      3   /* 二进制信号量 */
#define RTOS_QUEUE_TYPE_RECURSIVE   4   /* 递归互斥锁 */

/* ---- 队列结构体（用户可直接静态分配） ---- */
struct rtos_queue {
    /* 环形缓冲区指针（参考 kfifo 分段 memcpy 思想 + FreeRTOS 指针回绕） */
    uint8_t *buffer;            /* 缓冲区起始 */
    uint8_t *buffer_end;        /* 缓冲区末尾（越界，不做访问） */
    uint8_t *write_to;          /* 下一次尾部写入位置 */
    uint8_t *read_from;         /* 逻辑上前一个元素的位置（为 front-insert 预留） */

    /* 状态 */
    uint32_t length;            /* 队列容量（最大元素个数） */
    uint32_t item_size;         /* 单个元素字节数；0 表示“无数据”队列 */
    uint32_t messages_waiting;  /* 当前元素数 / 信号量 count */

    /* 阻塞链表（按优先级降序排列，高优先级在前） */
    struct rtos_list_node tasks_waiting_to_send;
    struct rtos_list_node tasks_waiting_to_receive;

    /* 队列类型标记 */
    uint8_t queue_type;
};

/* ============================================================
 * 🏗️ 生命周期
 * ============================================================ */

/**
 * @brief 初始化队列（用户提供内存，零堆依赖）
 * @param queue      队列结构体（用户静态分配）
 * @param buffer     数据缓冲区；item_size==0 时可传 NULL
 * @param length     队列长度（最大元素个数，必须 >= 1）
 * @param item_size  单个元素字节数；传 0 表示无数据队列
 * @return RTOS_OK / RTOS_ERR_PARAM
 *
 * @note  对于 item_size > 0，buffer 大小必须 >= length * item_size。
 */
rtos_err_t rtos_queue_init(struct rtos_queue *queue, void *buffer,
                           uint32_t length, uint32_t item_size);

/**
 * @brief 删除队列
 * @note  用户负责释放 buffer 和 queue 结构体内存。
 *        删除前必须确保没有任务阻塞在该队列上（断言检查）。
 */
void rtos_queue_delete(struct rtos_queue *queue);

/* ============================================================
 * 📨 统一发送 / 接收（任务上下文，可阻塞）
 * ============================================================ */

/**
 * @brief 统一发送入口
 * @param queue     目标队列
 * @param item      待发送数据指针；item_size==0 时可传 NULL
 * @param timeout   阻塞超时（tick）；RTOS_DONT_WAIT=不阻塞，RTOS_WAIT_FOREVER=永久
 * @param pos       发送位置（尾部/头部/覆盖）
 * @return RTOS_OK / RTOS_ERR_RESOURCE / RTOS_ERR_TIMEOUT
 */
rtos_err_t rtos_queue_generic_send(struct rtos_queue *queue,
                                   const void *item,
                                   uint32_t timeout,
                                   rtos_queue_send_pos_t pos);

/**
 * @brief 统一接收入口
 * @param queue     目标队列
 * @param buffer    接收缓冲区指针；item_size==0 时可传 NULL
 * @param timeout   阻塞超时
 * @param peek      true=只看不取（不修改队列），false=正常取出
 * @return RTOS_OK / RTOS_ERR_RESOURCE / RTOS_ERR_TIMEOUT
 */
rtos_err_t rtos_queue_generic_recv(struct rtos_queue *queue,
                                   void *buffer,
                                   uint32_t timeout,
                                   bool peek);

/* ============================================================
 * 🍬 便捷 inline 封装（零成本语法糖）
 * ============================================================ */

static inline rtos_err_t rtos_queue_send(struct rtos_queue *queue,
                                         const void *item,
                                         uint32_t timeout)
{
    return rtos_queue_generic_send(queue, item, timeout, RTOS_QUEUE_SEND_BACK);
}

static inline rtos_err_t rtos_queue_send_to_front(struct rtos_queue *queue,
                                                  const void *item,
                                                  uint32_t timeout)
{
    return rtos_queue_generic_send(queue, item, timeout, RTOS_QUEUE_SEND_FRONT);
}

static inline rtos_err_t rtos_queue_overwrite(struct rtos_queue *queue,
                                              const void *item)
{
    return rtos_queue_generic_send(queue, item, RTOS_DONT_WAIT,
                                   RTOS_QUEUE_SEND_OVERWRITE);
}

static inline rtos_err_t rtos_queue_recv(struct rtos_queue *queue,
                                         void *buffer,
                                         uint32_t timeout)
{
    return rtos_queue_generic_recv(queue, buffer, timeout, false);
}

static inline rtos_err_t rtos_queue_peek(struct rtos_queue *queue,
                                         void *buffer,
                                         uint32_t timeout)
{
    return rtos_queue_generic_recv(queue, buffer, timeout, true);
}

/* ============================================================
 * ⚡ 中断安全版本（不阻塞，返回 pxHigherPrioTaskWoken）
 * ============================================================ */

rtos_err_t rtos_queue_generic_send_from_isr(struct rtos_queue *queue,
                                            const void *item,
                                            rtos_queue_send_pos_t pos,
                                            bool *pxHigherPrioTaskWoken);

rtos_err_t rtos_queue_generic_recv_from_isr(struct rtos_queue *queue,
                                            void *buffer,
                                            bool *pxHigherPrioTaskWoken);

/* ============================================================
 * 📊 状态查询
 * ============================================================ */

static inline uint32_t rtos_queue_messages_waiting(struct rtos_queue *queue)
{
    return queue->messages_waiting;
}

static inline uint32_t rtos_queue_spaces_available(struct rtos_queue *queue)
{
    return queue->length - queue->messages_waiting;
}

static inline bool rtos_queue_is_empty(struct rtos_queue *queue)
{
    return queue->messages_waiting == 0;
}

static inline bool rtos_queue_is_full(struct rtos_queue *queue)
{
    return queue->messages_waiting >= queue->length;
}

#ifdef __cplusplus
}
#endif

#endif /* RTOS_QUEUE_H */
