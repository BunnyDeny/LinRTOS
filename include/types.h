/*
 * LinRTOS - Lightweight preemptive RTOS for ARM Cortex-M.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_TYPES_H
#define RTOS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 🎯 基础类型
 * ============================================================ */

typedef void *rtos_task_handle_t;

typedef void (*rtos_task_func_t)(void *param);
typedef void (*rtos_idle_hook_t)(void);

/* ============================================================
 * 📌 错误码
 * ============================================================ */

typedef enum {
    RTOS_OK                 = 0,
    RTOS_ERR_FAIL           = -1,
    RTOS_ERR_TIMEOUT        = -2,
    RTOS_ERR_RESOURCE       = -3,
    RTOS_ERR_PARAM          = -4,
    RTOS_ERR_ISR            = -5,
    RTOS_ERR_STATE          = -6,
    RTOS_ERR_NOMEM          = -7,
    RTOS_ERR_BUSY           = -8,
    RTOS_ERR_ABORTED        = -9,
    RTOS_ERR_OVERFLOW       = -10,
} rtos_err_t;

/* ============================================================
 * 🔹 任务状态
 * ============================================================ */

typedef enum {
    RTOS_TASK_READY     = 0,
    RTOS_TASK_RUNNING   = 1,
    RTOS_TASK_BLOCKED   = 2,
    RTOS_TASK_SUSPENDED = 3,
    RTOS_TASK_DELETED   = 4,
} rtos_task_state_t;

/* ============================================================
 * ⚙️ 时间常量
 * ============================================================ */

#define RTOS_WAIT_FOREVER       0xFFFFFFFFU
#define RTOS_DONT_WAIT          0U

#ifdef __cplusplus
}
#endif

#endif /* RTOS_TYPES_H */
