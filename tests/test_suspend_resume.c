/*
 * Test: Task suspend and resume
 *
 * 验证项：
 *  - rtos_task_suspend(task) 挂起其他任务
 *  - rtos_task_resume(task)  恢复其他任务
 *  - rtos_task_suspend(NULL) 自挂起
 *  - 挂起后任务停止调度，恢复后继续运行
 *  - 恢复高优先级任务时触发抢占
 */

#include "linRTOS.h"

#ifdef TEST_SUSPEND_RESUME

extern void debug_printf(const char *fmt, ...);

/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_ctrl_stack[128];
static uint32_t task_worker_stack[128];
static uint32_t task_self_suspend_stack[128];

static volatile uint32_t s_worker_count = 0;
static volatile uint32_t s_self_suspend_count = 0;
static rtos_task_handle_t h_worker = NULL;
static rtos_task_handle_t h_self_suspend = NULL;

/* ============================================================
 * 工作者任务 —— 低优先级，持续计数
 *
 * 控制任务会周期性挂起/恢复它，验证计数是否停止/继续。
 * ============================================================ */

static void task_worker(void *param)
{
    (void)param;
    for (;;) {
        s_worker_count++;
        debug_printf("[WORK] tick=%lu count=%lu\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (unsigned long)s_worker_count);
        rtos_task_delay(100);
    }
}

/* ============================================================
 * 自挂起任务 —— 中优先级
 *
 * 运行 3 次后自挂起，等待控制任务恢复。
 * ============================================================ */

static void task_self_suspend(void *param)
{
    (void)param;
    for (;;) {
        s_self_suspend_count++;
        debug_printf("[SELF] tick=%lu count=%lu\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (unsigned long)s_self_suspend_count);

        if (s_self_suspend_count >= 3) {
            debug_printf("[SELF] auto-suspending myself\r\n");
            s_self_suspend_count = 0;
            rtos_task_suspend(NULL);   /* 自挂起 */
            /* 恢复后继续从这里执行 */
            debug_printf("[SELF] resumed!\r\n");
        }
        rtos_task_delay(200);
    }
}

/* ============================================================
 * 控制任务 —— 高优先级
 *
 * 周期性地挂起/恢复工作者，并恢复自挂起任务。
 * ============================================================ */

static void task_ctrl(void *param)
{
    (void)param;
    int cycle = 0;
    for (;;) {
        cycle++;
        debug_printf("[CTRL] cycle=%d tick=%lu worker=%lu\r\n",
                     cycle,
                     (unsigned long)rtos_get_tick_count(),
                     (unsigned long)s_worker_count);

        /* 每 2 个周期切换工作者的挂起/恢复状态 */
        if (cycle % 2 == 1) {
            debug_printf("[CTRL] suspending worker\r\n");
            rtos_task_suspend(h_worker);
        } else {
            debug_printf("[CTRL] resuming worker\r\n");
            rtos_task_resume(h_worker);
        }

        /* 每 3 个周期恢复一次自挂起任务 */
        if (cycle % 3 == 0) {
            debug_printf("[CTRL] resuming self-suspend task\r\n");
            rtos_task_resume(h_self_suspend);
        }

        rtos_task_delay(500);
    }
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    debug_printf("=== Test: Suspend/Resume ===\r\n");

    rtos_task_create(task_worker,       "worker",       task_worker_stack,       128, NULL, 1, &h_worker);
    rtos_task_create(task_self_suspend, "self_suspend", task_self_suspend_stack, 128, NULL, 2, &h_self_suspend);
    rtos_task_create(task_ctrl,         "ctrl",         task_ctrl_stack,         128, NULL, 3, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_SUSPEND_RESUME */
