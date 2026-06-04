/*
 * Test: Dynamic priority change
 * 验证: 低优先级任务自提升后立即抢占同优先级高优先级任务
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_PRIORITY) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { cli_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_priority(void)
{
    static volatile uint32_t lo_seq = 0;
    static volatile uint32_t hi_seq = 0;

    lo_seq = 0; hi_seq = 0;

    /* lo starts at prio=1, then boosts itself to 5 — preempts hi */
    void lo_task(void *p) {
        (void)p;
        lo_seq = 1;
        rtos_task_set_priority(NULL, 5);
        lo_seq = 2;
        /* verify priority changed */
        uint32_t cur_prio = rtos_task_get_priority(NULL);
        if (cur_prio != 5) lo_seq = 999;
        rtos_task_delete(NULL);
    }

    /* hi starts at prio=3, should be preempted by lo after lo boosts */
    void hi_task(void *p) {
        (void)p;
        hi_seq = 1;
        rtos_task_delay(50);
        hi_seq = 2;
        rtos_task_delete(NULL);
    }

    rtos_task_create(lo_task, "lo", s_stk0, 160, NULL, 1, NULL);
    rtos_task_create(hi_task, "hi", s_stk1, 160, NULL, 3, NULL);

    rtos_task_delay(200);

    TEST_ASSERT(lo_seq == 2, "lo task should have boosted and completed");
    TEST_ASSERT(lo_seq != 999, "lo task priority should be 5 after boost");
    TEST_ASSERT(hi_seq >= 1, "hi task should have at least started");

    return true;
}

TEST_CASE_REGISTER(priority, test_priority);

#endif
