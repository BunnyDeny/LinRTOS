/*
 * Test: Counting semaphore — multi producer
 * 验证: init_counting, multi-give, multi-take, max-count boundary
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_semaphore.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_SEMAPHORE_COUNTING)

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];
extern uint32_t s_stk2[160];

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

static bool test_semaphore_counting(void)
{
    static struct rtos_queue sem;
    static volatile uint32_t pcnt, ccnt;

    pcnt = 0; ccnt = 0;

    void p1(void *p) {
        (void)p;
        for (int i = 0; i < 3; i++) {
            rtos_task_delay(20);
            rtos_semaphore_give(&sem);
            pcnt++;
        }
        rtos_task_delete(NULL);
    }

    void p2(void *p) {
        (void)p;
        for (int i = 0; i < 3; i++) {
            rtos_task_delay(25);
            rtos_semaphore_give(&sem);
            pcnt++;
        }
        rtos_task_delete(NULL);
    }

    void consumer(void *p) {
        (void)p;
        for (int i = 0; i < 6; i++) {
            rtos_err_t e = rtos_semaphore_take(&sem, RTOS_WAIT_FOREVER);
            if (e == RTOS_OK) ccnt++;
        }
        rtos_task_delete(NULL);
    }

    /* max=5, initial=0 */
    rtos_semaphore_init_counting(&sem, 5, 0);
    TEST_ASSERT(rtos_semaphore_get_count(&sem) == 0, "initial count should be 0");

    rtos_task_create(consumer, "cons", s_stk2, 160, NULL, 5, NULL);
    rtos_task_create(p1, "p1", s_stk0, 160, NULL, 2, NULL);
    rtos_task_create(p2, "p2", s_stk1, 160, NULL, 3, NULL);

    if (!wait_for_val(&pcnt, 6, 2000)) TEST_ASSERT(0, "timeout producers");
    if (!wait_for_val(&ccnt, 6, 2000)) TEST_ASSERT(0, "timeout consumer");

    TEST_ASSERT(pcnt == 6, "producer count should be 6");
    TEST_ASSERT(ccnt == 6, "consumer count should be 6 (all takes succeeded)");

    /* verify max-count: try to give beyond max, should fail */
    for (int i = 0; i < 5; i++) rtos_semaphore_give(&sem);
    rtos_err_t e = rtos_semaphore_give(&sem);
    TEST_ASSERT(e == RTOS_ERR_RESOURCE, "give beyond max should fail");

    rtos_semaphore_delete(&sem);
    return true;
}

TEST_CASE_REGISTER(semaphore_counting, test_semaphore_counting);

#endif
