/*
 * Test: Scheduler lock (single + nested)
 * 验证: lock 期间高优先级任务不抢占, unlock 后立即抢占, 嵌套锁逐层释放
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_SCHED_LOCK) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_sched_lock(void)
{
    static volatile bool hi_ran = false;


    void hi_task(void *p) {
        (void)p;
        hi_ran = true;
        rtos_task_delete(NULL);
    }

    /* ---- Test 1: single-level lock ---- */
    hi_ran = false;
    rtos_sched_lock();
    rtos_task_create(hi_task, "hi", s_stk0, 160, NULL, 5, NULL);
    rtos_task_delay(100);
    TEST_ASSERT(!hi_ran, "single lock: hi task must NOT preempt");

    rtos_sched_unlock();
    rtos_task_delay(50);
    TEST_ASSERT(hi_ran, "single unlock: hi task must preempt after unlock");

    /* ---- Test 2: nested lock (3 levels) ---- */
    hi_ran = false;
    rtos_sched_lock();  /* lock depth: 1 */
    rtos_sched_lock();  /* lock depth: 2 */
    rtos_sched_lock();  /* lock depth: 3 */
    rtos_task_create(hi_task, "hi2", s_stk0, 160, NULL, 5, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(!hi_ran, "nested lock (3): hi must NOT preempt");

    rtos_sched_unlock();  /* depth: 3→2 */
    rtos_task_delay(50);
    TEST_ASSERT(!hi_ran, "nested: after 1st unlock (depth=2), hi must NOT preempt");

    rtos_sched_unlock();  /* depth: 2→1 */
    rtos_task_delay(50);
    TEST_ASSERT(!hi_ran, "nested: after 2nd unlock (depth=1), hi must NOT preempt");

    rtos_sched_unlock();  /* depth: 1→0 — real unlock */
    rtos_task_delay(50);
    TEST_ASSERT(hi_ran, "nested: after 3rd unlock (depth=0), hi must preempt");

    return true;
}

TEST_CASE_REGISTER(sched_lock, test_sched_lock);

#endif
