/*
 * LinRTOS - Lightweight preemptive RTOS for ARM Cortex-M.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

/* Kconfig generated configuration — must be generated before build */
#include "linrtos_kconfig.h"

/* ============================================================
 * 🔒 固定配置（不参与 Kconfig）
 * ============================================================ */

/* 栈填充魔数（用于栈溢出检测） */
#ifndef RTOS_STACK_FILL_MAGIC
#define RTOS_STACK_FILL_MAGIC       0xA5A5A5A5U
#endif

#endif /* RTOS_CONFIG_H */
