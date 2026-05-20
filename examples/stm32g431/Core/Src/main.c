/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : LinRTOS demo for STM32G431
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "usart.h"
#include "gpio.h"
#include "rtos.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

extern void debug_puts(const char *str);
extern void debug_printf(const char *fmt, ...);

static rtos_mutex_handle_t uart_mutex;

/* ============================================================
 * 🪶 任务栈
 * ============================================================ */
static uint32_t task_high_stack[256];
static uint32_t task_low_stack[256];

/* ============================================================
 * 📋 高优先级任务 — 快速心跳
 * ============================================================ */
static void task_high(void *param)
{
    (void)param;
    int cnt = 0;
    for (;;) {
        rtos_mutex_take(uart_mutex, RTOS_WAIT_FOREVER);
        debug_printf("[HIGH] tick=%lu count=%d\r\n", (unsigned long)rtos_get_tick_count(), cnt++);
        rtos_mutex_give(uart_mutex);

        rtos_task_delay(500);   /* 500 ms */
    }
}

/* ============================================================
 * 📋 低优先级任务 — 慢速心跳
 * ============================================================ */
static void task_low(void *param)
{
    (void)param;
    int cnt = 0;
    for (;;) {
        rtos_mutex_take(uart_mutex, RTOS_WAIT_FOREVER);
        debug_printf("[LOW ] tick=%lu count=%d\r\n", (unsigned long)rtos_get_tick_count(), cnt++);
        rtos_mutex_give(uart_mutex);

        rtos_task_delay(1000);  /* 1000 ms */
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    /* USER CODE BEGIN 1 */
    /* USER CODE END 1 */

    /* MCU Configuration--------------------------------------------------------*/
    HAL_Init();
    SystemClock_Config();

    /* 设置 PendSV 优先级为最低（15），防止在 SysTick 中抢占导致 MSP 栈帧被覆盖 */
    *(volatile uint8_t *)0xE000ED22 = 0xF0;

    /* USER CODE BEGIN SysInit */
    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART3_UART_Init();

    /* USER CODE BEGIN 2 */
    debug_puts("\r\n=== LinRTOS on STM32G431 ===\r\n");
    debug_puts("Starting scheduler...\r\n");

    rtos_mutex_create(&uart_mutex);

    rtos_task_create(task_high, "high",
                     task_high_stack, sizeof(task_high_stack) / sizeof(uint32_t),
                     NULL, 2, NULL);

    rtos_task_create(task_low, "low",
                     task_low_stack, sizeof(task_low_stack) / sizeof(uint32_t),
                     NULL, 1, NULL);

    rtos_scheduler_start();
    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */
        /* USER CODE BEGIN 3 */
    }
    /* USER CODE END 3 */
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

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
