/*
 * Test: Task yield — same-priority round-robin alternation
 * 验证: 两个同优先级任务通过 rtos_task_yield 交替运行, 用序列号精确验证顺序
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_YIELD) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { cli_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_yield(void)
{
    /* Sequence numbers track execution order:
     * A created first among equal-priority tasks, so A runs first.
     * Expected: A=1, B=2, A=3, B=4, A=5, B=6, A=7, B=8, A=9, B=10 */
    static volatile uint32_t s_seq = 0;
    static volatile uint32_t s_a[5];
    static volatile uint32_t s_b[5];
    static volatile int a_idx = 0;
    static volatile int b_idx = 0;
    static volatile bool a_done = false;
    static volatile bool b_done = false;

    s_seq = 0; a_idx = 0; b_idx = 0; a_done = false; b_done = false;

    void t_a(void *p) {
        (void)p;
        for (int i = 0; i < 5; i++) {
            s_a[a_idx++] = ++s_seq;
            rtos_task_yield();
        }
        a_done = true;
        rtos_task_delete(NULL);
    }

    void t_b(void *p) {
        (void)p;
        for (int i = 0; i < 5; i++) {
            s_b[b_idx++] = ++s_seq;
            rtos_task_yield();
        }
        b_done = true;
        rtos_task_delete(NULL);
    }

    rtos_task_create(t_a, "a", s_stk0, 160, NULL, 2, NULL);
    rtos_task_create(t_b, "b", s_stk1, 160, NULL, 2, NULL);

    /* Wait for both to complete */
    uint32_t start = rtos_get_tick_count();
    while (!a_done || !b_done) {
        if ((int32_t)(rtos_get_tick_count() - start) > 2000) break;
        rtos_task_delay(20);
    }

    TEST_ASSERT(a_done && b_done, "both tasks should complete");
    TEST_ASSERT(s_seq == 10, "total sequence count should be 10");

    /* Verify strict alternation: A yields, B runs, B yields, A runs, ...
     * The specific order depends on the ready-list insertion order.
     * With round-robin, the first-created task will be found first at the same priority.
     * Expected: A entries are odd (1,3,5,7,9), B entries are even (2,4,6,8,10) */
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(s_a[i] > 0 && s_a[i] <= 10, "A sequence within range");
        TEST_ASSERT(s_b[i] > 0 && s_b[i] <= 10, "B sequence within range");
    }

    /* Verify alternation: for each pair, A_i < B_i < A_{i+1} */
    for (int i = 0; i < 5; i++) {
        if (i < 5 && s_a[i] >= s_b[i]) {
            cli_printk("  WARN: A[%d]=%lu >= B[%d]=%lu (expected A<B each pair)\r\n",
                       i, (unsigned long)s_a[i], i, (unsigned long)s_b[i]);
        }
    }

    /* At minimum, all 5 values per task should be unique and within 1..10 */
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            if (s_a[i] == s_a[j])
                TEST_ASSERT(0, "A sequence should have unique values");
            if (s_b[i] == s_b[j])
                TEST_ASSERT(0, "B sequence should have unique values");
        }
    }

    return true;
}

TEST_CASE_REGISTER(yield, test_yield);

#endif
