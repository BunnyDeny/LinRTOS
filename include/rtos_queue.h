/*
 * LinRTOS - Queue / message passing API.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_QUEUE_H
#define RTOS_QUEUE_H

#include "rtos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 消息队列（复制语义，线程安全） */
rtos_err_t rtos_queue_create(rtos_queue_handle_t *queue,
                             uint32_t item_size_bytes,
                             uint32_t capacity);
void rtos_queue_delete(rtos_queue_handle_t queue);
rtos_err_t rtos_queue_send(rtos_queue_handle_t queue,
                           const void *item,
                           uint32_t timeout_ticks);
rtos_err_t rtos_queue_send_isr(rtos_queue_handle_t queue,
                               const void *item,
                               int *needs_switch);
rtos_err_t rtos_queue_receive(rtos_queue_handle_t queue,
                              void *buffer,
                              uint32_t timeout_ticks);
rtos_err_t rtos_queue_receive_isr(rtos_queue_handle_t queue,
                                  void *buffer,
                                  int *needs_switch);
uint32_t rtos_queue_get_count(rtos_queue_handle_t queue);
uint32_t rtos_queue_get_spaces(rtos_queue_handle_t queue);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_QUEUE_H */
