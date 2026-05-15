/*
 * LinRTOS - Mutex API (with optional priority inheritance).
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_MUTEX_H
#define RTOS_MUTEX_H

#include "rtos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 递归互斥锁 */
rtos_err_t rtos_mutex_create(rtos_mutex_handle_t *mutex);
void rtos_mutex_delete(rtos_mutex_handle_t mutex);
rtos_err_t rtos_mutex_take(rtos_mutex_handle_t mutex, uint32_t timeout_ticks);
rtos_err_t rtos_mutex_give(rtos_mutex_handle_t mutex);
rtos_task_handle_t rtos_mutex_get_holder(rtos_mutex_handle_t mutex);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_MUTEX_H */
