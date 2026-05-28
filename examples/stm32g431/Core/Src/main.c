/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : LinRTOS 测试入口 —— 硬件初始化 + 启动 app_entry
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "linRTOS.h"
#include "cli_io.h"
#include "cli_critical.h"

/* USER CODE BEGIN PV */
uint8_t s_rx_buf[UART_RX_BUF_SIZE];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART3_UART_Init();

    sys_printk("=== LinRTOS Test Boot ===\r\n");

    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, s_rx_buf, UART_RX_BUF_SIZE);


    rtos_scheduler_start();

    /* 永远不会到达 */
    for (;;);
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV6;
    RCC_OscInitStruct.PLL.PLLN = 85;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_DMA_Init(void)
{
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
}

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
    if (s_cli_critical_nest > 0 && --s_cli_critical_nest == 0) {
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

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART3) {
        cli_in_push((_u8 *)s_rx_buf, (int)Size);
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
