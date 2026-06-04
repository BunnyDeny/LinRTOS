/*
 * Test: Mutex priority inheritance
 * 验证: 低优先级持锁时若高优先级阻塞，低优先级被提升；释放后恢复原始优先级
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_mutex.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_MUTEX_PRIORITY) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { cli_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool wait_bool(volatile bool *f, bool e, uint32_t to)
{
    uint32_t start = rtos_get_tick_count();
    while (*f != e) {
        if ((int32_t)(rtos_get_tick_count() - start) >= (int32_t)to) return false;
        rtos_task_delay(10);
    }
    return true;
}

static bool test_mutex_priority(void)
{
    static struct rtos_queue mtx;
    static volatile uint32_t l_prio, l_prio_after;
    static volatile bool high_done;

    l_prio = 0; l_prio_after = 0; high_done = false;

    void low_task(void *p) {
        (void)p;
        rtos_err_t e = rtos_mutex_take(&mtx, RTOS_WAIT_FOREVER);
        if (e != RTOS_OK) { rtos_task_delete(NULL); return; }
        cli_printk("  low got mutex, prio=%lu\r\n",
                   (unsigned long)rtos_task_get_priority(NULL));
        /* delay to let high_task run and block on the mutex */
        rtos_task_delay(100);
        l_prio = rtos_task_get_priority(NULL);
        cli_printk("  low prio during hold = %lu\r\n", (unsigned long)l_prio);
        e = rtos_mutex_give(&mtx);
        if (e != RTOS_OK) { rtos_task_delete(NULL); return; }
        l_prio_after = rtos_task_get_priority(NULL);
        cli_printk("  low prio after give = %lu\r\n", (unsigned long)l_prio_after);
        rtos_task_delete(NULL);
    }

    void high_task(void *p) {
        (void)p;
        rtos_task_delay(30);
        cli_printk("  high trying to take mutex...\r\n");
        rtos_err_t e = rtos_mutex_take(&mtx, RTOS_WAIT_FOREVER);
        if (e != RTOS_OK) { rtos_task_delete(NULL); return; }
        cli_printk("  high got mutex\r\n");
        rtos_mutex_give(&mtx);
        high_done = true;
        rtos_task_delete(NULL);
    }

    rtos_mutex_init(&mtx);

    /* low prio=2 created first (gets mutex), high prio=5 blocks on it */
    rtos_task_create(low_task,  "low",  s_stk0, 160, NULL, 2, NULL);
    rtos_task_create(high_task, "high", s_stk1, 160, NULL, 5, NULL);

    if (!wait_bool(&high_done, true, 2000))
        TEST_ASSERT(0, "timeout waiting for high task");

    TEST_ASSERT(l_prio == 5, "low priority should be boosted to 5");
    TEST_ASSERT(l_prio_after == 2, "low priority should be restored to 2");

    rtos_mutex_delete(&mtx);
    return true;
}

TEST_CASE_REGISTER(mutex_priority, test_mutex_priority);

#endif
