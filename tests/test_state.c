/*
 * Test: Task state query
 * 验证: rtos_task_get_state / get_priority / get_current
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_STATE)

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_state(void)
{
    static rtos_task_handle_t h_helper;
    static volatile rtos_task_state_t s_self_state;
    static volatile uint32_t s_self_prio;
    static volatile rtos_task_handle_t s_self_handle;
    static volatile rtos_task_state_t s_helper_state1;
    static volatile rtos_task_state_t s_helper_state2;

    sys_printk("[%s]\r\n", __func__);
    h_helper = NULL;

    void task_query(void *p) {
        (void)p;
        s_self_state = rtos_task_get_state(NULL);
        s_self_prio  = rtos_task_get_priority(NULL);
        s_self_handle = rtos_task_get_current();

        s_helper_state1 = rtos_task_get_state(h_helper);
        rtos_task_delay(500);
        s_helper_state2 = rtos_task_get_state(h_helper);

        rtos_task_delete(NULL);
    }

    void task_helper(void *p) {
        (void)p;
        for (;;) rtos_task_delay(10000);
    }

    /* helper must be higher priority than query:
     * helper runs first, enters delay(10000) → BLOCKED,
     * then query runs and can observe the BLOCKED state. */
    rtos_task_create(task_helper, "helper", s_stk1, 160, NULL, 3, &h_helper);
    rtos_task_create(task_query,  "query",  s_stk0, 160, NULL, 2, NULL);
    rtos_task_delay(600);

    sys_printk("  self state=%d prio=%lu handle=%p\r\n",
               (int)s_self_state, (unsigned long)s_self_prio, (void *)s_self_handle);
    sys_printk("  helper state1=%d state2=%d\r\n",
               (int)s_helper_state1, (int)s_helper_state2);

    TEST_ASSERT(s_self_state == RTOS_TASK_RUNNING, "self should be RUNNING(1)");
    TEST_ASSERT(s_self_prio == 2, "self priority should be 2");
    TEST_ASSERT(s_self_handle != NULL, "get_current should return non-NULL");
    TEST_ASSERT(s_helper_state1 == RTOS_TASK_BLOCKED, "helper should be BLOCKED(2)");
    TEST_ASSERT(s_helper_state2 == RTOS_TASK_BLOCKED, "helper still BLOCKED after 500");

    rtos_task_delete(h_helper);
    return true;
}

TEST_CASE_REGISTER(state, test_state);

#endif
