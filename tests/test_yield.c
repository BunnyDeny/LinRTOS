/*
 * Test: Task yield
 *
 * 验证项：
 *  - rtos_task_yield 同优先级主动让出 CPU
 */

#include "linRTOS.h"

#ifdef TEST_YIELD

extern void debug_printf(const char *fmt, ...);

/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_a_stack[128];
static uint32_t task_b_stack[128];

/* ============================================================
 * 同优先级任务 A —— 通过 yield 与 B 交替运行
 * ============================================================ */

static void task_a(void *param)
{
    (void)param;
    for (int i = 0; i < 5; i++) {
        RTOS_ENTER_CRITICAL();
        debug_printf("[A   ] tick=%lu yield->B\r\n",
                     (unsigned long)rtos_get_tick_count());
        RTOS_EXIT_CRITICAL();
        rtos_task_yield();
    }
    RTOS_ENTER_CRITICAL();
    debug_printf("[A   ] yield test done\r\n");
    RTOS_EXIT_CRITICAL();
    rtos_task_delete(NULL);
}

/* ============================================================
 * 同优先级任务 B —— 通过 yield 与 A 交替运行
 * ============================================================ */

static void task_b(void *param)
{
    (void)param;
    for (int i = 0; i < 5; i++) {
        RTOS_ENTER_CRITICAL();
        debug_printf("[B   ] tick=%lu yield->A\r\n",
                     (unsigned long)rtos_get_tick_count());
        RTOS_EXIT_CRITICAL();
        rtos_task_yield();
    }
    RTOS_ENTER_CRITICAL();
    debug_printf("[B   ] yield test done\r\n");
    RTOS_EXIT_CRITICAL();
    rtos_task_delete(NULL);
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    debug_printf("=== Test: Task Yield ===\r\n");

    rtos_task_create(task_a, "A", task_a_stack, 128, NULL, 2, NULL);
    rtos_task_create(task_b, "B", task_b_stack, 128, NULL, 2, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_YIELD */
