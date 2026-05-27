/*
 * LinRTOS - Unified Queue / IPC primitive implementation.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 *
 * 设计要点：
 *   - 单一结构体承载消息队列、信号量、互斥锁等多种语义。
 *   - item_size == 0 时不搬数据，仅操作 messages_waiting 计数。
 *   - 环形缓冲区采用 FreeRTOS 风格的指针回绕（不依赖 2 的幂）。
 *   - memcpy 的分段技巧参考 LinCLI kfifo（计算到 buffer_end 距离，尾部+头部）。
 *   - 阻塞链表按优先级降序排列，唤醒时直接取第一个即可。
 */

#include <string.h>
#include "linRTOS.h"
#include "rtos_queue.h"
#include "port.h"

/* ============================================================
 * 🔒 内部断言快捷方式
 * ============================================================ */

#define QUEUE_ASSERT(cond)  RTOS_ASSERT(cond)

/* ============================================================
 * 📦 数据拷贝辅助（必须在临界区内调用）
 * ============================================================ */

/**
 * @brief 将 item 写入队列（send 路径）
 *
 * send_to_back:   写到 write_to，然后指针回绕
 * send_to_front:  从 read_from 往前退一个 item，写到那里
 * overwrite:      直接覆盖 read_from 处的元素（length 必须为 1）
 */
static void prv_copy_data_to_queue(struct rtos_queue *q,
                                   const void *item,
                                   rtos_queue_send_pos_t pos)
{
    if (q->item_size == 0) {
        /* 信号量/互斥锁：不搬数据 */
        return;
    }

    if (pos == RTOS_QUEUE_SEND_BACK) {
        /* 尾部追加 */
        uint32_t first_chunk = (uint32_t)(q->buffer_end - q->write_to);
        if (first_chunk >= q->item_size) {
            /* 整个 item 在尾部连续放得下 */
            memcpy(q->write_to, item, q->item_size);
        } else {
            /* 分段：尾部一段 + 头部一段（kfifo 风格） */
            memcpy(q->write_to, item, first_chunk);
            memcpy(q->buffer, (const uint8_t *)item + first_chunk,
                   q->item_size - first_chunk);
        }
        q->write_to += q->item_size;
        if (q->write_to >= q->buffer_end) {
            q->write_to = q->buffer;
        }
    }
    else if (pos == RTOS_QUEUE_SEND_FRONT) {
        /* 头部插入：先写到当前 read_from，再把指针往前退
         * 这样下次 recv 时 read_from += item_size 正好读到这个新数据 */
        memcpy(q->read_from, item, q->item_size);
        q->read_from -= q->item_size;
        if (q->read_from < q->buffer) {
            q->read_from = q->buffer_end - q->item_size;
        }
    }
    else { /* RTOS_QUEUE_SEND_OVERWRITE */
        /* 直接覆盖 read_from 位置的元素 */
        memcpy(q->read_from, item, q->item_size);
    }
}

/**
 * @brief 从队列读取 item 到 buffer（recv 路径）
 * @param peek  true=只看不取（不移动 read_from）
 */
static void prv_copy_data_from_queue(struct rtos_queue *q,
                                     void *buffer,
                                     bool peek)
{
    if (q->item_size == 0) {
        return;
    }

    /* 计算下一个读取位置 */
    uint8_t *read_pos = q->read_from + q->item_size;
    if (read_pos >= q->buffer_end) {
        read_pos = q->buffer;
    }

    /* 拷贝数据（同样处理分段，虽然单 item 通常不会跨边界） */
    uint32_t first_chunk = (uint32_t)(q->buffer_end - read_pos);
    if (first_chunk >= q->item_size) {
        memcpy(buffer, read_pos, q->item_size);
    } else {
        memcpy(buffer, read_pos, first_chunk);
        memcpy((uint8_t *)buffer + first_chunk, q->buffer,
               q->item_size - first_chunk);
    }

    if (!peek) {
        q->read_from = read_pos;
    }
}

/* ============================================================
 * ⏸️ 阻塞 / 唤醒辅助（必须在临界区内调用）
 * ============================================================ */

/**
 * @brief 将当前任务按优先级降序插入事件链表
 */
static void prv_insert_task_to_event_list(struct rtos_list_node *event_list,
                                          struct rtos_tcb *tcb)
{
    struct rtos_list_node *pos;
    rtos_list_for_each(pos, event_list) {
        struct rtos_tcb *p = rtos_list_entry(pos, struct rtos_tcb, event_node);
        /* 优先级数值越大越高，插入到第一个比当前低的任务之前 */
        if (tcb->priority > p->priority) {
            rtos_list_insert_before(pos, &tcb->event_node);
            return;
        }
    }
    /* 没有比当前优先级更低的，插入到尾部 */
    rtos_list_insert_before(event_list, &tcb->event_node);
}

/**
 * @brief 将当前任务阻塞在指定事件链表上（可附带超时）
 */
static void prv_block_current_task(struct rtos_list_node *event_list,
                                   uint32_t timeout)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)rtos_current_tcb;

    tcb->event_list = event_list;
    prv_insert_task_to_event_list(event_list, tcb);

    if (timeout != RTOS_WAIT_FOREVER) {
        tcb->wake_tick = g_kernel.tick_count + timeout;

        /* 双列表分流：跨越 tick 回绕边界的任务放入 overflow 列表 */
        struct rtos_list_node *target_list;
        if (tcb->wake_tick < g_kernel.tick_count) {
            target_list = g_kernel.px_overflow_delayed_task_list;
        } else {
            target_list = g_kernel.px_delayed_task_list;
        }

        /* 在目标列表中按 wake_tick 升序找插入位置 */
        struct rtos_list_node *pos;
        rtos_list_for_each(pos, target_list) {
            struct rtos_tcb *p = rtos_list_entry(pos, struct rtos_tcb, delay_node);
            if ((int32_t)(p->wake_tick - tcb->wake_tick) > 0) {
                break;
            }
        }
        rtos_list_insert_before(pos, &tcb->delay_node);
    }

    rtos_task_unready(tcb);
}

/**
 * @brief 从事件链表头部唤醒最高优先级任务
 * @return 被唤醒的任务指针；NULL=无任务等待
 */
static struct rtos_tcb *prv_wake_highest_from_event_list(
    struct rtos_list_node *event_list)
{
    if (rtos_list_is_empty(event_list)) {
        return NULL;
    }

    struct rtos_list_node *node = event_list->next;
    struct rtos_tcb *tcb = rtos_list_entry(node, struct rtos_tcb, event_node);

    rtos_list_remove(&tcb->event_node);
    tcb->event_list = NULL;
    tcb->wakeup_reason = 1;  /* 正常唤醒 */

    /* 若同时挂在 delay_list 上（设置了超时），一并移除 */
    if (tcb->delay_node.next != &tcb->delay_node) {
        rtos_list_remove(&tcb->delay_node);
    }

    rtos_task_ready(tcb);
    return tcb;
}

/* ============================================================
 * 🏗️ 生命周期
 * ============================================================ */

rtos_err_t rtos_queue_init(struct rtos_queue *q, void *buffer,
                           uint32_t length, uint32_t item_size)
{
    if (!q || length == 0) {
        return RTOS_ERR_PARAM;
    }
    if (item_size > 0 && !buffer) {
        return RTOS_ERR_PARAM;
    }

    memset(q, 0, sizeof(*q));

    if (item_size == 0) {
        /* 无数据队列（信号量/互斥锁）：buffer 指向自身，避免 NULL */
        q->buffer = (uint8_t *)q;
    } else {
        q->buffer = (uint8_t *)buffer;
    }

    q->buffer_end = q->buffer + (length * item_size);
    q->write_to   = q->buffer;

    if (item_size > 0) {
        /* read_from 初始指向最后一个 item 的位置，使第一次 recv 时
         * read_pos = read_from + item_size 回绕到 buffer，正好读到
         * 第一次 send_to_back 写入的数据。 */
        q->read_from = q->buffer + ((length - 1) * item_size);
    } else {
        q->read_from = q->buffer;
    }

    q->length           = length;
    q->item_size        = item_size;
    q->messages_waiting = 0;
    q->queue_type       = RTOS_QUEUE_TYPE_BASE;

    rtos_list_init(&q->tasks_waiting_to_send);
    rtos_list_init(&q->tasks_waiting_to_receive);

    return RTOS_OK;
}

void rtos_queue_delete(struct rtos_queue *q)
{
    QUEUE_ASSERT(q);
    /* 安全起见：删除前必须确保没有任务阻塞在该队列上 */
    QUEUE_ASSERT(rtos_list_is_empty(&q->tasks_waiting_to_send));
    QUEUE_ASSERT(rtos_list_is_empty(&q->tasks_waiting_to_receive));

    /* 用户负责释放 buffer 和结构体内存 */
}

/* ============================================================
 * 📨 统一发送（任务上下文）
 * ============================================================ */

rtos_err_t rtos_queue_generic_send(struct rtos_queue *q,
                                   const void *item,
                                   uint32_t timeout,
                                   rtos_queue_send_pos_t pos)
{
    QUEUE_ASSERT(q);
    QUEUE_ASSERT(!((item == NULL) && (q->item_size != 0)));
    QUEUE_ASSERT(!((pos == RTOS_QUEUE_SEND_OVERWRITE) && (q->length != 1)));

    for (;;) {
        RTOS_ENTER_CRITICAL();

        /* 有空间？overwrite 总是可以写入 */
        if ((q->messages_waiting < q->length) ||
            (pos == RTOS_QUEUE_SEND_OVERWRITE)) {

            prv_copy_data_to_queue(q, item, pos);

            if (pos == RTOS_QUEUE_SEND_OVERWRITE) {
                /* overwrite：若队列已有元素，count 不变；若为空则变为 1 */
                if (q->messages_waiting == 0) {
                    q->messages_waiting = 1;
                }
            } else {
                q->messages_waiting++;
            }

            /* 唤醒等待接收的最高优先级任务 */
            struct rtos_tcb *woken =
                prv_wake_highest_from_event_list(&q->tasks_waiting_to_receive);

            RTOS_EXIT_CRITICAL();

            if (woken &&
                woken->priority >
                    ((struct rtos_tcb *)rtos_current_tcb)->priority) {
                rtos_sched();
            }
            return RTOS_OK;
        }

        /* 队列满且不允许阻塞 */
        if (timeout == RTOS_DONT_WAIT) {
            RTOS_EXIT_CRITICAL();
            return RTOS_ERR_RESOURCE;
        }

        /* 阻塞到发送等待链表 */
        prv_block_current_task(&q->tasks_waiting_to_send, timeout);
        RTOS_EXIT_CRITICAL();
        rtos_sched();

        /* 被唤醒后：检查是正常唤醒还是超时 */
        struct rtos_tcb *tcb = (struct rtos_tcb *)rtos_current_tcb;
        if (tcb->wakeup_reason == 2) {
            tcb->wakeup_reason = 0;
            return RTOS_ERR_TIMEOUT;
        }
        tcb->wakeup_reason = 0;
        /* 正常唤醒，继续循环尝试发送 */
    }
}

/* ============================================================
 * 📥 统一接收（任务上下文）
 * ============================================================ */

rtos_err_t rtos_queue_generic_recv(struct rtos_queue *q,
                                   void *buffer,
                                   uint32_t timeout,
                                   bool peek)
{
    QUEUE_ASSERT(q);
    QUEUE_ASSERT(!((buffer == NULL) && (q->item_size != 0)));

    for (;;) {
        RTOS_ENTER_CRITICAL();

        if (q->messages_waiting > 0) {
            prv_copy_data_from_queue(q, buffer, peek);

            if (!peek) {
                q->messages_waiting--;

                /* 唤醒等待发送的最高优先级任务 */
                struct rtos_tcb *woken =
                    prv_wake_highest_from_event_list(&q->tasks_waiting_to_send);

                RTOS_EXIT_CRITICAL();

                if (woken &&
                    woken->priority >
                        ((struct rtos_tcb *)rtos_current_tcb)->priority) {
                    rtos_sched();
                }
            } else {
                RTOS_EXIT_CRITICAL();
            }
            return RTOS_OK;
        }

        /* 队列空且不允许阻塞 */
        if (timeout == RTOS_DONT_WAIT) {
            RTOS_EXIT_CRITICAL();
            return RTOS_ERR_RESOURCE;
        }

        /* 阻塞到接收等待链表 */
        prv_block_current_task(&q->tasks_waiting_to_receive, timeout);
        RTOS_EXIT_CRITICAL();
        rtos_sched();

        /* 被唤醒后：检查是正常唤醒还是超时 */
        struct rtos_tcb *tcb = (struct rtos_tcb *)rtos_current_tcb;
        if (tcb->wakeup_reason == 2) {
            tcb->wakeup_reason = 0;
            return RTOS_ERR_TIMEOUT;
        }
        tcb->wakeup_reason = 0;
        /* 正常唤醒，继续循环尝试接收 */
    }
}

/* ============================================================
 * ⚡ 中断安全版本
 * ============================================================ */

rtos_err_t rtos_queue_generic_send_from_isr(struct rtos_queue *q,
                                            const void *item,
                                            rtos_queue_send_pos_t pos,
                                            bool *pxHigherPrioTaskWoken)
{
    QUEUE_ASSERT(q);
    QUEUE_ASSERT(rtos_port_is_in_isr());
    QUEUE_ASSERT(!((item == NULL) && (q->item_size != 0)));
    QUEUE_ASSERT(!((pos == RTOS_QUEUE_SEND_OVERWRITE) && (q->length != 1)));

    RTOS_ENTER_CRITICAL();

    if ((q->messages_waiting < q->length) ||
        (pos == RTOS_QUEUE_SEND_OVERWRITE)) {

        prv_copy_data_to_queue(q, item, pos);

        if (pos == RTOS_QUEUE_SEND_OVERWRITE) {
            if (q->messages_waiting == 0) {
                q->messages_waiting = 1;
            }
        } else {
            q->messages_waiting++;
        }

        struct rtos_tcb *woken =
            prv_wake_highest_from_event_list(&q->tasks_waiting_to_receive);

        RTOS_EXIT_CRITICAL();

        if (woken && pxHigherPrioTaskWoken) {
            if (woken->priority >
                ((struct rtos_tcb *)rtos_current_tcb)->priority) {
                *pxHigherPrioTaskWoken = true;
            }
        }
        return RTOS_OK;
    }

    RTOS_EXIT_CRITICAL();
    return RTOS_ERR_RESOURCE;
}

rtos_err_t rtos_queue_generic_recv_from_isr(struct rtos_queue *q,
                                            void *buffer,
                                            bool *pxHigherPrioTaskWoken)
{
    QUEUE_ASSERT(q);
    QUEUE_ASSERT(rtos_port_is_in_isr());
    QUEUE_ASSERT(!((buffer == NULL) && (q->item_size != 0)));

    RTOS_ENTER_CRITICAL();

    if (q->messages_waiting > 0) {
        prv_copy_data_from_queue(q, buffer, false);
        q->messages_waiting--;

        struct rtos_tcb *woken =
            prv_wake_highest_from_event_list(&q->tasks_waiting_to_send);

        RTOS_EXIT_CRITICAL();

        if (woken && pxHigherPrioTaskWoken) {
            if (woken->priority >
                ((struct rtos_tcb *)rtos_current_tcb)->priority) {
                *pxHigherPrioTaskWoken = true;
            }
        }
        return RTOS_OK;
    }

    RTOS_EXIT_CRITICAL();
    return RTOS_ERR_RESOURCE;
}
