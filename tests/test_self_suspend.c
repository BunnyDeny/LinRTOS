/*
 * Test: Self-suspend suspend(NULL)
 * 验证: rtos_task_suspend(NULL) 自挂起, 外部 resume 后继续执行, 序列号精确追踪
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_SELF_SUSPEND) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { cli_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_self_suspend(void)
{
    static volatile uint32_t sscnt = 0;
    static volatile bool ctrl_done = false;
    static rtos_task_handle_t h_self;

    sscnt = 0; ctrl_done = false; h_self = NULL;

    void self_task(void *p) {
        (void)p;
        sscnt = 1;
        rtos_task_delay(50);
        sscnt = 2;
        rtos_task_suspend(NULL);
        /* externally resumed — continue execution */
        sscnt = 3;
        rtos_task_delete(NULL);
    }

    void ctrl_task(void *p) {
        (void)p;
        uint32_t start = rtos_get_tick_count();
        while (sscnt < 2) {
            if ((int32_t)(rtos_get_tick_count() - start) > 1000) { ctrl_done = true; rtos_task_delete(NULL); return; }
            rtos_task_delay(10);
        }
        /* verify suspended and state hasn't advanced past 2 */
        rtos_task_delay(50);
        if (sscnt != 2) { ctrl_done = true; rtos_task_delete(NULL); return; }

        /* resume */
        rtos_task_resume(h_self);

        start = rtos_get_tick_count();
        while (sscnt < 3) {
            if ((int32_t)(rtos_get_tick_count() - start) > 1000) break;
            rtos_task_delay(10);
        }
        ctrl_done = true;
        rtos_task_delete(NULL);
    }

    rtos_task_create(self_task, "self", s_stk0, 160, NULL, 1, &h_self);
    rtos_task_create(ctrl_task, "ctrl", s_stk1, 160, NULL, 3, NULL);
    rtos_task_delay(500);

    TEST_ASSERT(ctrl_done, "ctrl task should have completed");
    TEST_ASSERT(sscnt == 3, "self task should have resumed and completed (sscnt=3)");

    return true;
}

TEST_CASE_REGISTER(self_suspend, test_self_suspend);

#endif
