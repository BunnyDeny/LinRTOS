/*
 * Test: Dynamic priority change
 *
 * 验证项：
 *  - rtos_task_set_priority 动态调整优先级
 *  - 提升优先级后高优先级任务抢占当前任务
 *  - 降低优先级后当前任务被高优先级任务抢占
 */

#include "linRTOS.h"

#ifdef TEST_PRIORITY

extern void debug_printf(const char *fmt, ...);

/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_worker_stack[128];
static uint32_t task_ctrl_stack[128];

static rtos_task_handle_t h_worker = NULL;

/* ============================================================
 * 工作者任务 —— 打印确认自身被调度
 * ============================================================ */

static void task_worker(void *param)
{
    (void)param;
    for (;;) {
        uint32_t prio = rtos_task_get_priority(NULL);
        {
            RTOS_ENTER_CRITICAL();
            debug_printf("[WORK] tick=%lu prio=%lu running\r\n",
                         (unsigned long)rtos_get_tick_count(),
                         (unsigned long)prio);
            RTOS_EXIT_CRITICAL();
        }
        rtos_task_delay(300);
    }
}

/* ============================================================
 * 控制任务 —— 周期性地升降工作者优先级
 * ============================================================ */

static void task_ctrl(void *param)
{
    (void)param;
    int cycle = 0;

    for (;;) {
        cycle++;
        rtos_task_delay(600);

        /* 将工作者提升到最高优先级(3)，验证抢占 */
        {
            rtos_task_set_priority(h_worker, 3);
            
            RTOS_ENTER_CRITICAL();
            debug_printf("[CTRL] cycle=%d raise worker to prio 3\r\n", cycle);
            RTOS_EXIT_CRITICAL();
        }

        rtos_task_delay(600);

        /* 将工作者降到最低优先级(1)，验证被抢占 */
        {
            rtos_task_set_priority(h_worker, 1);
            
            RTOS_ENTER_CRITICAL();
            debug_printf("[CTRL] cycle=%d lower worker to prio 1\r\n", cycle);
            RTOS_EXIT_CRITICAL();
        }

        if (cycle >= 5) {
            {
                RTOS_ENTER_CRITICAL();
                debug_printf("[CTRL] test done, entering idle\r\n");
                RTOS_EXIT_CRITICAL();
            }
            for (;;) {
                rtos_task_delay(1000);
            }
        }
    }
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    {
        RTOS_ENTER_CRITICAL();
        debug_printf("=== Test: Priority Change ===\r\n");
        RTOS_EXIT_CRITICAL();
    }

    rtos_task_create(task_worker, "worker", task_worker_stack, 128, NULL, 2, &h_worker);
    rtos_task_create(task_ctrl,   "ctrl",   task_ctrl_stack,   128, NULL, 1, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_PRIORITY */
