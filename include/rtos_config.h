/*
 * LinRTOS - Lightweight preemptive RTOS for ARM Cortex-M.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

/* ============================================================
 * 🎯 可裁剪配置
 * ============================================================ */

/* 最大优先级数 (0 ~ RTOS_MAX_PRIORITIES-1)，数值越大优先级越高 */
#ifndef RTOS_MAX_PRIORITIES
#define RTOS_MAX_PRIORITIES     32
#endif

/* 系统节拍频率 (Hz) */
#ifndef RTOS_TICK_RATE_HZ
#define RTOS_TICK_RATE_HZ       1000
#endif

/* 任务名字最大长度 */
#ifndef RTOS_MAX_TASK_NAME_LEN
#define RTOS_MAX_TASK_NAME_LEN  16
#endif

/* 是否启用时间片轮转（同优先级任务之间） */
#ifndef RTOS_ENABLE_TIME_SLICING
#define RTOS_ENABLE_TIME_SLICING    1
#endif

/* 时间片长度（tick数） */
#ifndef RTOS_TIME_SLICE_TICKS
#define RTOS_TIME_SLICE_TICKS       1
#endif

/* 是否启用空闲钩子 */
#ifndef RTOS_ENABLE_IDLE_HOOK
#define RTOS_ENABLE_IDLE_HOOK       0
#endif

/* 栈填充魔数（用于栈溢出检测） */
#ifndef RTOS_STACK_FILL_MAGIC
#define RTOS_STACK_FILL_MAGIC       0xA5A5A5A5U
#endif

/* 中断嵌套中的安全临界区嵌套深度上限 */
#ifndef RTOS_MAX_CRITICAL_NESTING
#define RTOS_MAX_CRITICAL_NESTING   256
#endif

#endif /* RTOS_CONFIG_H */
