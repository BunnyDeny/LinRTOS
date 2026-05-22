/*
 * Test: Self-delete + TCB recycle
 *
 * 验证项：
 *  - 任务自删除后空闲任务正确回收 TCB
 *  - 回收的 TCB 可被后续创建的任务重用
 *  - 高优先级任务提供抢占干扰
 */

#include "linRTOS.h"

#ifdef TEST_SELFDELETE

extern void debug_printf(const char *fmt, ...);

/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_high_stack[256];
static uint32_t task_low_stack[256];
static uint32_t task_test_stack[128];

static volatile uint32_t s_high_count = 0;
static volatile uint32_t s_low_count = 0;
static volatile uint32_t s_test_count = 0;

/* ============================================================
 * 高优先级任务 —— 计数 + delay，制造抢占干扰
 * ============================================================ */

static void task_high(void *param)
{
    (void)param;
    for (;;) {
        s_high_count++;
        RTOS_ENTER_CRITICAL();
        debug_printf("[HIGH] tick=%lu count=%lu\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (unsigned long)s_high_count);
        RTOS_EXIT_CRITICAL();
        rtos_task_delay(500);
    }
}

/* ============================================================
 * 自删任务 —— 运行一次后自删，验证空闲任务回收 TCB
 * ============================================================ */

static void task_test(void *param)
{
    (void)param;
    s_test_count++;
    {
        RTOS_ENTER_CRITICAL();
        debug_printf("[TEST] run #%lu, tick=%lu\r\n",
                     (unsigned long)s_test_count,
                     (unsigned long)rtos_get_tick_count());
        RTOS_EXIT_CRITICAL();
    }
    rtos_task_delay(300);
    {
        RTOS_ENTER_CRITICAL();
        debug_printf("[TEST] self-delete\r\n");
        RTOS_EXIT_CRITICAL();
    }
    rtos_task_delete(NULL);
}

/* ============================================================
 * 低优先级任务 —— 周期性创建自删任务，验证 TCB 回收重用
 * ============================================================ */

static void task_low(void *param)
{
    (void)param;
    for (;;) {
        s_low_count++;
        {
            RTOS_ENTER_CRITICAL();
            debug_printf("[LOW ] tick=%lu count=%lu test_count=%lu\r\n",
                         (unsigned long)rtos_get_tick_count(),
                         (unsigned long)s_low_count,
                         (unsigned long)s_test_count);
            RTOS_EXIT_CRITICAL();
        }
        rtos_err_t err = rtos_task_create(task_test, "test",
                                          task_test_stack, 128, NULL, 2, NULL);
        {
            RTOS_ENTER_CRITICAL();
            if (err != RTOS_OK) {
                debug_printf("[LOW ] create test FAILED! err=%d (pool exhausted?)\r\n",
                             (int)err);
            } else {
                debug_printf("[LOW ] create test OK\r\n");
            }
            RTOS_EXIT_CRITICAL();
        }
        rtos_task_delay(1000);
    }
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    RTOS_ENTER_CRITICAL();
    debug_printf("=== Test: Self-Delete Recycle ===\r\n");
    RTOS_EXIT_CRITICAL();

    rtos_task_create(task_high, "high", task_high_stack, 128, NULL, 3, NULL);
    rtos_task_create(task_low,  "low",  task_low_stack,  128, NULL, 1, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_SELFDELETE */
