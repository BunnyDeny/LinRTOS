/*
 * LinCLI Port Layer for LinRTOS on STM32G431.
 */

#include "linRTOS.h"
#include "cli_critical.h"
#include "usart.h"

/* ============================================================
 * 🛡️ 临界区 —— 映射到 LinRTOS PRIMASK 临界区
 * ============================================================ */

static uint32_t s_cli_critical_state;
static int s_cli_critical_nest = 0;

void cli_enter_critical(void)
{
    if (s_cli_critical_nest++ == 0) {
        s_cli_critical_state = rtos_port_enter_critical();
    }
}

void cli_exit_critical(void)
{
    if (--s_cli_critical_nest == 0) {
        rtos_port_exit_critical(s_cli_critical_state);
    }
}

/* ============================================================
 * 🖨️ 字符输出 —— 直接调用 HAL UART 发送一个字符
 * ============================================================ */

void cli_putc(char ch)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, 100);
}
