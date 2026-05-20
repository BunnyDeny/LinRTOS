/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : LinRTOS 极简示例 —— 任务延时与优先级抢占调度
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "rtos.h"

/* USER CODE BEGIN PV */
static uint32_t task_high_stack[128];
static uint32_t task_low_stack[128];

static volatile uint32_t s_high_count = 0;
static volatile uint32_t s_low_count = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);

/* USER CODE BEGIN 0 */
extern void debug_printf(const char *fmt, ...);
/* USER CODE END 0 */

/* USER CODE BEGIN 1 */
static void task_high(void *param)
{
    (void)param;
    for (;;) {
        s_high_count++;
        debug_printf("[HIGH] tick=%lu count=%lu\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (unsigned long)s_high_count);
        rtos_task_delay(500);
    }
}

static void task_low(void *param)
{
    (void)param;
    for (;;) {
        s_low_count++;
        debug_printf("[LOW ] tick=%lu count=%lu\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (unsigned long)s_low_count);
        rtos_task_delay(1000);
    }
}
/* USER CODE END 1 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* 关键：将 PendSV 设为最低优先级，防止在 SysTick 中抢占导致 MSP 栈帧被破坏 */
    *(volatile uint8_t *)0xE000ED22 = 0xF0;

    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART3_UART_Init();

    rtos_task_create(task_high, "high", task_high_stack, 128, NULL, 2, NULL);
    rtos_task_create(task_low,  "low",  task_low_stack,  128, NULL, 1, NULL);

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

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
