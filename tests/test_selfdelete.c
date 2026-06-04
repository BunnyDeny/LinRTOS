/*
 * Test: Self-delete and TCB recycle
 * 验证: 任务自删除后 TCB 被空闲任务回收, 后续 create 可重用
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_SELFDELETE) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { cli_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_selfdelete(void)
{
    static volatile int created_count = 0;

    created_count = 0;

    void self_deleter(void *p) {
        (void)p;
        created_count++;
        rtos_task_delete(NULL);
    }

    /* First creation and self-delete */
    rtos_task_create(self_deleter, "sd", s_stk0, 160, NULL, 2, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(created_count == 1, "first self-delete should have run");

    /* Second creation — same stack/handle reused, TCB should be recycled */
    rtos_task_handle_t h;
    rtos_err_t e = rtos_task_create(self_deleter, "sd2", s_stk0, 160, NULL, 2, &h);
    TEST_ASSERT(e == RTOS_OK, "second create should succeed (TCB recycled)");
    rtos_task_delay(50);
    TEST_ASSERT(created_count == 2, "second self-delete should have run");

    /* Third — verify pool not exhausted */
    e = rtos_task_create(self_deleter, "sd3", s_stk0, 160, NULL, 2, NULL);
    TEST_ASSERT(e == RTOS_OK, "third create should succeed");
    rtos_task_delay(50);
    TEST_ASSERT(created_count == 3, "third self-delete should have run");

    return true;
}

TEST_CASE_REGISTER(selfdelete, test_selfdelete);

#endif
