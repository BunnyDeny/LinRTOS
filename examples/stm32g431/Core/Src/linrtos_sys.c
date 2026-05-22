/*
 * LinRTOS STM32G4 Adapter — SysTick + UART debug.
 */

#include "stm32g4xx_hal.h"
#include "usart.h"
#include "kernel.h"
#include "linRTOS.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* ============================================================
 * ⏱️ SysTick — 同时服务 HAL 和 LinRTOS
 * ============================================================ */

void SysTick_Handler(void)
{
    HAL_IncTick();
    rtos_tick_handler();
}

/* ============================================================
 * 🖨️ 串口调试输出（阻塞发送，简短调用）
 * ============================================================ */

static char s_debug_buf[256];

void debug_puts(const char *str)
{
    if (!str) return;
    HAL_UART_Transmit(&huart3, (uint8_t *)str, (uint16_t)strlen(str), 100);
}

void debug_printf(const char *fmt, ...)
{
    RTOS_ENTER_CRITICAL();
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_debug_buf, sizeof(s_debug_buf), fmt, ap);
    va_end(ap);
    debug_puts(s_debug_buf);
    RTOS_EXIT_CRITICAL();
}
