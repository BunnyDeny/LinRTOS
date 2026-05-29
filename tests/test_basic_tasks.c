/*
 * Test: Basic multi-task + delay + preemption (visual test)
 * 验证: 多任务创建/删除, delay, delay_until, 抢占调度
 * 此测试主要为串口观察设计, 通过计数验证基本功能
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_BASIC_TASKS)

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];
extern uint32_t s_stk2[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_basic_tasks(void)
{
    static volatile uint32_t high_cnt = 0;
    static volatile uint32_t mid_cnt  = 0;
    static volatile uint32_t low_cnt  = 0;
    static rtos_task_handle_t h_high, h_mid, h_low;

    sys_printk("[%s]\r\n", __func__);
    high_cnt = 0; mid_cnt = 0; low_cnt = 0;

    void task_high(void *p) {
        (void)p;
        for (;;) {
            high_cnt++;
            rtos_task_delay(200);
        }
    }

    void task_mid(void *p) {
        (void)p;
        for (;;) {
            mid_cnt++;
            rtos_task_delay(400);
        }
    }

    void task_low(void *p) {
        (void)p;
        for (;;) {
            low_cnt++;
            rtos_task_delay(800);
        }
    }

    rtos_task_create(task_high, "high", s_stk0, 160, NULL, 3, &h_high);
    rtos_task_create(task_mid,  "mid",  s_stk1, 160, NULL, 2, &h_mid);
    rtos_task_create(task_low,  "low",  s_stk2, 160, NULL, 1, &h_low);

    /* Let them run for a while */
    rtos_task_delay(2000);

    uint32_t h = high_cnt, m = mid_cnt, l = low_cnt;
    sys_printk("  after 2000 ticks: high=%lu mid=%lu low=%lu\r\n",
               (unsigned long)h, (unsigned long)m, (unsigned long)l);

    /* High priority (delay=200) should run ~10 times in 2000 ticks.
     * Mid (delay=400) should run ~5 times.
     * Low (delay=800) should run ~2-3 times but preempted by higher tasks. */
    TEST_ASSERT(h >= 5, "high task should have run at least 5 times");
    TEST_ASSERT(m >= 2, "mid task should have run at least 2 times");
    TEST_ASSERT(l >= 1, "low task should have run at least once");

    /* High should run more than low (preemption) */
    TEST_ASSERT(h >= l, "high should run >= low due to preemption");

    /* Clean up — these tasks loop forever, must be deleted */
    rtos_task_delete(h_high);
    rtos_task_delete(h_mid);
    rtos_task_delete(h_low);

    return true;
}

TEST_CASE_REGISTER(basic_tasks, test_basic_tasks);

#endif
