/*
 * LinRTOS - Software timer API.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_TIMER_H
#define RTOS_TIMER_H

#include "rtos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 定时器类型 */
typedef enum {
    RTOS_TIMER_ONE_SHOT = 0,
    RTOS_TIMER_AUTO_RELOAD = 1,
} rtos_timer_mode_t;

rtos_err_t rtos_timer_create(rtos_timer_handle_t *timer,
                             const char *name,
                             uint32_t period_ticks,
                             rtos_timer_mode_t mode,
                             void *arg,
                             rtos_timer_callback_t callback);
void rtos_timer_delete(rtos_timer_handle_t timer);
rtos_err_t rtos_timer_start(rtos_timer_handle_t timer);
rtos_err_t rtos_timer_stop(rtos_timer_handle_t timer);
rtos_err_t rtos_timer_reset(rtos_timer_handle_t timer);
rtos_err_t rtos_timer_change_period(rtos_timer_handle_t timer,
                                    uint32_t new_period_ticks);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_TIMER_H */
