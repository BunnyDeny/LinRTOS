/*
 * Test: Stack free query
 * 验证: rtos_task_get_stack_free 返回合理范围内的值
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_STACK_FREE) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_stack_free(void)
{
    static volatile uint32_t free_a = 0;
    static volatile uint32_t free_b = 0;

    free_a = 0; free_b = 0;

    void task_a(void *p) {
        (void)p;
        free_a = rtos_task_get_stack_free(NULL);
        sys_printk("  A stack free=%lu words\r\n", (unsigned long)free_a);
        rtos_task_delete(NULL);
    }

    void task_b(void *p) {
        (void)p;
        free_b = rtos_task_get_stack_free(NULL);
        sys_printk("  B stack free=%lu words\r\n", (unsigned long)free_b);
        rtos_task_delete(NULL);
    }

    rtos_task_create(task_a, "a", s_stk0, 160, NULL, 1, NULL);
    rtos_task_create(task_b, "b", s_stk1, 160, NULL, 2, NULL);
    rtos_task_delay(100);

    TEST_ASSERT(free_a > 0, "stack free A should be > 0");
    TEST_ASSERT(free_a <= 160, "stack free A should be <= 160");
    TEST_ASSERT(free_b > 0, "stack free B should be > 0");
    TEST_ASSERT(free_b <= 160, "stack free B should be <= 160");

    /* With stack depth 160 and minimal task body, expect >100 words free */
    TEST_ASSERT(free_a > 100, "stack free A should be >100 (light usage)");
    TEST_ASSERT(free_b > 100, "stack free B should be >100 (light usage)");

    return true;
}

TEST_CASE_REGISTER(stack_free, test_stack_free);

#endif
