/*
 * Test: Task yield
 *
 * 验证项：
 *  - rtos_task_yield 同优先级主动让出 CPU
 */

#include "linRTOS.h"
#include "cli_io.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_YIELD)


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
    pr_debug("[A   ] tick=%lu yield->B\r\n",
                     (unsigned long)rtos_get_tick_count());
        rtos_task_yield();
    }
    pr_debug("[A   ] yield test done\r\n");
    rtos_task_delete(NULL);
}

/* ============================================================
 * 同优先级任务 B —— 通过 yield 与 A 交替运行
 * ============================================================ */

static void task_b(void *param)
{
    (void)param;
    for (int i = 0; i < 5; i++) {
    pr_debug("[B   ] tick=%lu yield->A\r\n",
                     (unsigned long)rtos_get_tick_count());
        rtos_task_yield();
    }
    pr_debug("[B   ] yield test done\r\n");
    rtos_task_delete(NULL);
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    pr_debug("=== Test: Task Yield ===\r\n");

    rtos_task_create(task_a, "A", task_a_stack, 128, NULL, 2, NULL);
    rtos_task_create(task_b, "B", task_b_stack, 128, NULL, 2, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_YIELD */
