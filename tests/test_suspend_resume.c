/*
 * Test: Task suspend and resume (external)
 * 验证: rtos_task_suspend / rtos_task_resume, 挂起期间任务不执行, 恢复后继续执行
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_SUSPEND_RESUME) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_suspend_resume(void)
{
    static volatile uint32_t s_seq = 0;
    static rtos_task_handle_t h_task;

    s_seq = 0; h_task = NULL;

    void task_func(void *p) {
        (void)p;
        s_seq = 1;
        rtos_task_delay(100);
        s_seq = 2;
        rtos_task_delete(NULL);
    }

    rtos_err_t err = rtos_task_create(task_func, "t", s_stk0, 160, NULL, 2, &h_task);
    TEST_ASSERT(err == RTOS_OK, "task create should succeed");
    TEST_ASSERT(h_task != NULL, "handle should not be NULL");

    /* Suspend immediately — task should not execute */
    rtos_task_suspend(h_task);
    rtos_task_delay(200);
    TEST_ASSERT(s_seq == 0, "suspended task should not have run");

    /* Verify state is SUSPENDED */
    rtos_task_state_t st = rtos_task_get_state(h_task);
    TEST_ASSERT(st == RTOS_TASK_SUSPENDED, "task should be SUSPENDED");

    /* Resume — task should complete */
    rtos_task_resume(h_task);
    rtos_task_delay(200);
    TEST_ASSERT(s_seq == 2, "resumed task should have completed all steps");

    return true;
}

TEST_CASE_REGISTER(suspend_resume, test_suspend_resume);

#endif
