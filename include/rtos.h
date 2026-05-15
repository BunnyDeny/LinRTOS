/*
 * LinRTOS - Master header.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_H
#define RTOS_H

#include "rtos_types.h"
#include "rtos_config.h"
#include "rtos_task.h"
#include "rtos_sem.h"
#include "rtos_mutex.h"
#include "rtos_queue.h"
#include "rtos_timer.h"
#include "rtos_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 🚀 调度器启动
 * ============================================================ */

/* 创建空闲任务并启动调度（此函数不会返回） */
void rtos_scheduler_start(void);

/* ============================================================
 * 🛡️ 临界区快捷宏（基于移植层实现）
 * ============================================================ */

#define RTOS_ENTER_CRITICAL()   \
    uint32_t _critical_state = rtos_port_enter_critical()

#define RTOS_EXIT_CRITICAL()    \
    rtos_port_exit_critical(_critical_state)

/* 函数声明来自 rtos_port.h */
uint32_t rtos_port_enter_critical(void);
void rtos_port_exit_critical(uint32_t state);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_H */
