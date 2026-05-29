/*
 * Test: Binary semaphore — producer / consumer sync
 * 验证: init_binary, give, take, semaphore count
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_semaphore.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_SEMAPHORE_BINARY) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];

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

static bool test_semaphore_binary(void)
{
    static struct rtos_queue sem;
    static volatile uint32_t pcnt, ccnt;

    pcnt = 0; ccnt = 0;

    void producer(void *p) {
        (void)p;
        for (int i = 0; i < 5; i++) {
            rtos_task_delay(30);
            rtos_semaphore_give(&sem);
            pcnt++;
        }
        rtos_task_delete(NULL);
    }

    void consumer(void *p) {
        (void)p;
        for (int i = 0; i < 5; i++) {
            rtos_err_t e = rtos_semaphore_take(&sem, RTOS_WAIT_FOREVER);
            if (e == RTOS_OK) ccnt++;
        }
        rtos_task_delete(NULL);
    }

    rtos_semaphore_init_binary(&sem);

    /* consumer first — blocks; producer second — wakes consumer */
    rtos_task_create(consumer, "cons", s_stk1, 160, NULL, 5, NULL);
    rtos_task_create(producer, "prod", s_stk0, 160, NULL, 2, NULL);

    if (!wait_for_val(&pcnt, 5, 2000)) TEST_ASSERT(0, "timeout producer");
    if (!wait_for_val(&ccnt, 5, 2000)) TEST_ASSERT(0, "timeout consumer");

    TEST_ASSERT(pcnt == 5, "producer count should be 5");
    TEST_ASSERT(ccnt == 5, "consumer count should be 5 (all takes succeeded)");
    TEST_ASSERT(rtos_semaphore_get_count(&sem) == 0, "sem count should be 0");

    rtos_semaphore_delete(&sem);
    return true;
}

TEST_CASE_REGISTER(semaphore_binary, test_semaphore_binary);

#endif
