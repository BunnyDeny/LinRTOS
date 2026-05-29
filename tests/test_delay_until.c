/*
 * Test: Absolute periodic delay (delay_until)
 * 验证: rtos_task_delay_until 周期精度, jitter 在可接受范围
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_DELAY_UNTIL) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool wait_for_val(volatile uint32_t *f, uint32_t e, uint32_t to)
{
    uint32_t start = rtos_get_tick_count();
    while (*f < e) {
        if ((int32_t)(rtos_get_tick_count() - start) >= (int32_t)to) return false;
        rtos_task_delay(10);
    }
    return true;
}

static bool test_delay_until(void)
{
    static volatile uint32_t du_count = 0;
    static volatile uint32_t du_tick[5];
    static volatile int32_t du_jitter[5];

    du_count = 0;

    void periodic(void *p) {
        (void)p;
        uint32_t prev = rtos_get_tick_count();
        for (int i = 0; i < 5; i++) {
            du_tick[i] = rtos_get_tick_count();
            du_jitter[i] = (int32_t)(du_tick[i] - prev);
            du_count++;
            rtos_task_delay_until(&prev, 100);
        }
        /* Record last wake time after final delay_until */
        rtos_task_delete(NULL);
    }

    rtos_task_create(periodic, "per", s_stk0, 160, NULL, 3, NULL);
    if (!wait_for_val(&du_count, 5, 2000))
        TEST_ASSERT(0, "timeout waiting for delay_until cycles");

    TEST_ASSERT(du_count == 5, "should complete 5 cycles");

    /* Verify period accuracy:
     * delay_until wakes at prev, so jitter from target should be ~0.
     * The interval between successive wake ticks should be ~100. */
    for (int i = 0; i < 5; i++) {
        sys_printk("  cycle %d tick=%lu jitter=%ld\r\n",
                   i, (unsigned long)du_tick[i], (long)du_jitter[i]);
        if (i > 0) {
            int32_t j = du_jitter[i];
            int32_t interval = (int32_t)(du_tick[i] - du_tick[i-1]);
            /* jitter from target should be small (< 5 ticks) */
            if (j < -5 || j > 5) {
                sys_printk("  WARN: cycle %d jitter=%ld > ±5 ticks\r\n", i, (long)j);
            }
            /* interval between wakes should be ~100 ticks */
            if (interval < 90 || interval > 110) {
                sys_printk("  WARN: cycle %d interval=%ld (expected ~100)\r\n", i, (long)interval);
            }
        }
    }

    /* Wait for periodic task to finish its final delay_until and delete itself */
    rtos_task_delay(120);

    return true;
}

TEST_CASE_REGISTER(delay_until, test_delay_until);

#endif
