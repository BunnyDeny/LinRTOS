/*
 * Test: Stack free query
 *
 * 验证项：
 *  - rtos_task_get_stack_free 查询剩余栈空间
 */

#include "linRTOS.h"
#include "cli_io.h"

#ifdef TEST_STACK_FREE


/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_a_stack[128];
static uint32_t task_b_stack[128];

/* ============================================================
 * 任务 A —— 周期性打印自身栈余量
 * ============================================================ */

static void task_a(void *param)
{
    (void)param;
    for (int i = 0; i < 3; i++) {
        uint32_t free = rtos_task_get_stack_free(NULL);
        pr_debug("[A   ] tick=%lu stack_free=%lu\r\n",
                         (unsigned long)rtos_get_tick_count(),
                         (unsigned long)free);
        rtos_task_delay(200);
    }
    pr_debug("[A   ] stack_free test done\r\n");
    rtos_task_delete(NULL);
}

/* ============================================================
 * 任务 B —— 周期性打印自身栈余量
 * ============================================================ */

static void task_b(void *param)
{
    (void)param;
    for (int i = 0; i < 3; i++) {
        uint32_t free = rtos_task_get_stack_free(NULL);
        pr_debug("[B   ] tick=%lu stack_free=%lu\r\n",
                         (unsigned long)rtos_get_tick_count(),
                         (unsigned long)free);
        rtos_task_delay(200);
    }
    pr_debug("[B   ] stack_free test done\r\n");
    rtos_task_delete(NULL);
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    pr_debug("=== Test: Stack Free ===\r\n");

    rtos_task_create(task_a, "A", task_a_stack, 128, NULL, 1, NULL);
    rtos_task_create(task_b, "B", task_b_stack, 128, NULL, 2, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_STACK_FREE */
