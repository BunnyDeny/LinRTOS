/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : LinRTOS FPU 验证示例 —— 浮点任务上下文切换测试
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "linRTOS.h"
#include <math.h>

/* USER CODE BEGIN PV */
static uint32_t task_high_stack[256];
static uint32_t task_low_stack[256];
static uint32_t task_fp_a_stack[512];
static uint32_t task_fp_b_stack[512];
static uint32_t task_test_stack[128];

static volatile uint32_t s_high_count = 0;
static volatile uint32_t s_low_count = 0;
static volatile uint32_t s_test_count = 0;
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

/* ============================================================
 * 🔄 自删任务 —— 运行一次后自删，验证空闲任务回收 TCB
 * ============================================================ */
static void task_test(void *param)
{
    (void)param;
    s_test_count++;
    debug_printf("[TEST] run #%lu, tick=%lu\r\n",
                 (unsigned long)s_test_count,
                 (unsigned long)rtos_get_tick_count());
    rtos_task_delay(300);
    debug_printf("[TEST] self-delete\r\n");
    rtos_task_delete(NULL);
}

static void task_low(void *param)
{
    (void)param;
    for (;;) {
        s_low_count++;
        debug_printf("[LOW ] tick=%lu count=%lu test_count=%lu\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (unsigned long)s_low_count,
                     (unsigned long)s_test_count);
        /* 周期性创建自删任务，验证 TCB 回收重用 */
        rtos_err_t err = rtos_task_create(task_test, "test",
                                          task_test_stack, 128, NULL, 2, NULL);
        if (err != RTOS_OK) {
            debug_printf("[LOW ] create test FAILED! err=%d (pool exhausted?)\r\n",
                         (int)err);
        } else {
            debug_printf("[LOW ] create test OK\r\n");
        }
        rtos_task_delay(1000);
    }
}

/* ============================================================
 * 🧮 浮点任务 A —— 三角函数累加验证
 * ============================================================
 *
 * 通过累加 + sinf/cosf/sqrtf 大量占用 FPU 寄存器，
 * 若上下文切换时 FPU 寄存器未正确保存，累加值会错乱。
 * ============================================================ */
static void task_fp_a(void *param)
{
    (void)param;
    float sum = 0.0f;
    for (;;) {
        sum += 0.1f;
        /* 大量使用 FPU 寄存器 */
        float s = sinf(sum);
        float c = cosf(sum);
        float chk = sqrtf(fabsf(s) + fabsf(c));
        debug_printf("[FPA ] tick=%lu sum=%.6f sin=%.6f cos=%.6f chk=%.6f\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (double)sum, (double)s, (double)c, (double)chk);
        rtos_task_delay(300);
    }
}

/* ============================================================
 * 🧮 浮点任务 B —— 乘幂/对数验证
 * ============================================================
 *
 * 与 task_fp_a 使用完全不同的浮点运算序列，
 * 若 FPU 上下文切换有 bug，两个任务的结果会互相污染。
 * ============================================================ */
static void task_fp_b(void *param)
{
    (void)param;
    float val = 1.0f;
    for (;;) {
        val = val * 1.618f + 0.314f;
        if (val > 500.0f) {
            val = 1.0f;
        }
        float t = tanf(val);
        float l = logf(val);
        debug_printf("[FPB ] tick=%lu val=%.6f tan=%.6f log=%.6f\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (double)val, (double)t, (double)l);
        rtos_task_delay(700);
    }
}
/* USER CODE END 1 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART3_UART_Init();

    debug_printf("=== LinRTOS FPU Test Boot ===\r\n");

    rtos_task_create(task_fp_a, "fp_a", task_fp_a_stack, 256, NULL, 3, NULL);
    rtos_task_create(task_high, "high", task_high_stack, 128, NULL, 2, NULL);
    rtos_task_create(task_fp_b, "fp_b", task_fp_b_stack, 256, NULL, 2, NULL);
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
