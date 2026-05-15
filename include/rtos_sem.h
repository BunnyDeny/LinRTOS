/*
 * LinRTOS - Semaphore API.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_SEM_H
#define RTOS_SEM_H

#include "rtos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 二值/计数信号量 */
rtos_err_t rtos_sem_create(rtos_sem_handle_t *sem, uint32_t initial_count);
void rtos_sem_delete(rtos_sem_handle_t sem);
rtos_err_t rtos_sem_take(rtos_sem_handle_t sem, uint32_t timeout_ticks);
rtos_err_t rtos_sem_give(rtos_sem_handle_t sem);
rtos_err_t rtos_sem_give_isr(rtos_sem_handle_t sem, int *needs_switch);
uint32_t rtos_sem_get_count(rtos_sem_handle_t sem);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_SEM_H */
