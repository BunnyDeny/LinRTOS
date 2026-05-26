/*
 * Test: Abort delay
 *
 * 验证项：
 *  - rtos_task_abort_delay 强制唤醒阻塞任务
 *  - rtos_scheduler_is_running 查询调度器状态
 */

#include "linRTOS.h"
#include "cli_io.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_ABORT_DELAY)


/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_low_stack[128];
static uint32_t task_ctrl_stack[128];

static rtos_task_handle_t h_low = NULL;

/* ============================================================
 * 低优先级任务 —— 长时间延时，被 abort_delay 强制唤醒
 * ============================================================ */

static void task_low(void *param)
{
    (void)param;
    for (;;) {
    sys_printk("[LOW ] delaying 1000 ticks\r\n");
        rtos_task_delay(1000);
        sys_printk("[LOW ] woken at tick=%lu\r\n",
                         (unsigned long)rtos_get_tick_count());
    }
}

/* ============================================================
 * 控制任务 —— 强制唤醒低优先级任务
 * ============================================================ */

static void task_ctrl(void *param)
{
    (void)param;

    sys_printk("[CTRL] scheduler is_running=%d\r\n",
                     rtos_scheduler_is_running());

    for (int i = 0; i < 3; i++) {
        rtos_task_delay(600);
        sys_printk("[CTRL] abort_delay low task (attempt %d)\r\n", i + 1);
        rtos_err_t err = rtos_task_abort_delay(h_low);
        if (err == RTOS_OK) {
        sys_printk("[CTRL] abort_delay OK\r\n");
        } else {
        sys_printk("[CTRL] abort_delay failed err=%d\r\n", (int)err);
        }
    }

    sys_printk("[CTRL] test done, entering idle\r\n");
    for (;;) {
        rtos_task_delay(1000);
    }
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    sys_printk("=== Test: Abort Delay ===\r\n");

    rtos_task_create(task_low, "low", task_low_stack, 128, NULL, 1, &h_low);
    rtos_task_create(task_ctrl, "ctrl", task_ctrl_stack, 128, NULL, 3, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_ABORT_DELAY */
