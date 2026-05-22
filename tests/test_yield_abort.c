/*
 * Test: Task yield + abort delay + stack free query
 *
 * 验证项：
 *  - rtos_task_yield 同优先级主动让出 CPU
 *  - rtos_task_abort_delay 强制唤醒阻塞任务
 *  - rtos_task_get_stack_free 查询剩余栈空间
 *  - rtos_scheduler_is_running 查询调度器状态
 */

#include "linRTOS.h"

#ifdef TEST_YIELD_ABORT

extern void debug_printf(const char *fmt, ...);

/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_a_stack[128];
static uint32_t task_b_stack[128];
static uint32_t task_low_stack[128];
static uint32_t task_ctrl_stack[128];

static rtos_task_handle_t h_low = NULL;

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
    for (;;) {
        rtos_task_delay(1000);
    }
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
    for (;;) {
        rtos_task_delay(1000);
    }
}

/* ============================================================
 * 低优先级任务 —— 长时间延时，被 abort_delay 强制唤醒
 * ============================================================ */

static void task_low(void *param)
{
    (void)param;
    for (;;) {
        uint32_t free = rtos_task_get_stack_free(NULL);
        debug_printf("[LOW ] delaying 1000 ticks, stack_free=%lu\r\n",
                     (unsigned long)free);
        rtos_task_delay(1000);
        debug_printf("[LOW ] woken at tick=%lu\r\n",
                     (unsigned long)rtos_get_tick_count());
    }
}

/* ============================================================
 * 控制任务 —— 强制唤醒低优先级任务
 * ============================================================ */

static void task_ctrl(void *param)
{
    (void)param;

    debug_printf("[CTRL] scheduler is_running=%d\r\n",
                 rtos_scheduler_is_running());

    for (int i = 0; i < 3; i++) {
        rtos_task_delay(600);
        debug_printf("[CTRL] abort_delay low task (attempt %d)\r\n", i + 1);
        rtos_err_t err = rtos_task_abort_delay(h_low);
        if (err == RTOS_OK) {
            debug_printf("[CTRL] abort_delay OK\r\n");
        } else {
            debug_printf("[CTRL] abort_delay failed err=%d\r\n", (int)err);
        }
    }

    debug_printf("[CTRL] test done, entering idle\r\n");
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

    debug_printf("=== Test: Yield + Abort Delay ===\r\n");

    rtos_task_create(task_a,   "A",   task_a_stack,   128, NULL, 2, NULL);
    rtos_task_create(task_b,   "B",   task_b_stack,   128, NULL, 2, NULL);
    rtos_task_create(task_low, "low", task_low_stack, 128, NULL, 1, &h_low);
    rtos_task_create(task_ctrl, "ctrl", task_ctrl_stack, 128, NULL, 3, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_YIELD_ABORT */
