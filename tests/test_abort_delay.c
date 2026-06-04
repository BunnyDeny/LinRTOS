/*
 * Test: Abort delay — force wake blocked task
 * 验证: rtos_task_abort_delay 将阻塞任务强制唤醒, 验证状态变化和时间
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_ABORT_DELAY) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { cli_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_abort_delay(void)
{
    static rtos_task_handle_t h_sleeper;
    static volatile uint32_t sleeper_wake_tick = 0;

    h_sleeper = NULL;
    sleeper_wake_tick = 0;

    void sleeper(void *p) {
        (void)p;
        cli_printk("  sleeper delaying 5000 ticks...\r\n");
        uint32_t t0 = rtos_get_tick_count();
        rtos_task_delay(5000);
        sleeper_wake_tick = rtos_get_tick_count() - t0;
        cli_printk("  sleeper woken after %lu ticks\r\n",
                   (unsigned long)sleeper_wake_tick);
        rtos_task_delete(NULL);
    }

    /* Verify scheduler is running */
    TEST_ASSERT(rtos_scheduler_is_running(), "scheduler should be running");

    rtos_task_create(sleeper, "sleep", s_stk0, 160, NULL, 2, &h_sleeper);

    /* Wait a bit, then abort the delay */
    rtos_task_delay(50);

    rtos_task_state_t st = rtos_task_get_state(h_sleeper);
    TEST_ASSERT(st == RTOS_TASK_BLOCKED, "sleeper should be BLOCKED before abort");

    rtos_err_t e = rtos_task_abort_delay(h_sleeper);
    TEST_ASSERT(e == RTOS_OK, "abort_delay should return OK");

    rtos_task_delay(50);

    /* After abort, task should no longer be BLOCKED */
    st = rtos_task_get_state(h_sleeper);
    TEST_ASSERT(st != RTOS_TASK_BLOCKED, "sleeper should NOT be BLOCKED after abort");

    /* The sleeper should have woken much sooner than 5000 ticks */
    TEST_ASSERT(sleeper_wake_tick > 0, "sleeper should report wake time");
    TEST_ASSERT(sleeper_wake_tick < 1000, "sleeper should wake well before 5000 ticks");

    return true;
}

TEST_CASE_REGISTER(abort_delay, test_abort_delay);

#endif
