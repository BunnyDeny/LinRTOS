/*
 * LinRTOS - Task management API.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_TASK_H
#define RTOS_TASK_H

#include "rtos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 📌 任务管理 API
 * ============================================================ */

/* 创建任务（用户提供栈空间，适合裸机静态内存布局） */
rtos_err_t rtos_task_create(rtos_task_func_t func,
                            const char *name,
                            uint32_t *stack_buffer,
                            uint32_t stack_depth_words,
                            void *param,
                            uint32_t priority,
                            rtos_task_handle_t *out_handle);

/* 删除任务（传入 NULL 删除自身） */
void rtos_task_delete(rtos_task_handle_t task);

/* 挂起任务 */
void rtos_task_suspend(rtos_task_handle_t task);

/* 恢复任务 */
void rtos_task_resume(rtos_task_handle_t task);

/* 延时指定 tick（相对） */
void rtos_task_delay(uint32_t ticks);

/* 延时到指定绝对 tick */
void rtos_task_delay_until(uint32_t *prev_wake_tick, uint32_t interval);

/* 主动放弃CPU（同优先级任务间协作） */
void rtos_task_yield(void);

/* 获取当前任务句柄 */
rtos_task_handle_t rtos_task_get_current(void);

/* 获取任务优先级 */
uint32_t rtos_task_get_priority(rtos_task_handle_t task);

/* 设置任务优先级 */
void rtos_task_set_priority(rtos_task_handle_t task, uint32_t priority);

/* 获取任务状态 */
rtos_task_state_t rtos_task_get_state(rtos_task_handle_t task);

/* 获取剩余栈空间（按字计） */
uint32_t rtos_task_get_stack_free(rtos_task_handle_t task);

/* 获取系统 tick 计数 */
uint32_t rtos_get_tick_count(void);

/* 获取调度器运行状态 */
int rtos_scheduler_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_TASK_H */
