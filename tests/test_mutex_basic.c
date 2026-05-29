/*
 * Test: Mutex basic — take/give/ownership
 * 验证: 互斥保护、holder 查询、非持有者 give 拒绝
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_mutex.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_MUTEX_BASIC)

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool wait_for_val(volatile uint32_t *flag, uint32_t expect, uint32_t to)
{
    uint32_t start = rtos_get_tick_count();
    while (*flag < expect) {
        if ((int32_t)(rtos_get_tick_count() - start) >= (int32_t)to) return false;
        rtos_task_delay(10);
    }
    return true;
}

static bool test_mutex_basic(void)
{
    static struct rtos_queue mtx;
    static volatile uint32_t s_shared, a_done, b_done;
    static volatile rtos_err_t bad_err;

    s_shared = 0; a_done = 0; b_done = 0; bad_err = RTOS_OK;

    void task_a(void *p) {
        (void)p;
        for (int i = 0; i < 5; i++) {
            rtos_err_t e = rtos_mutex_take(&mtx, RTOS_WAIT_FOREVER);
            if (e != RTOS_OK) return;
            uint32_t tmp = s_shared;
            rtos_task_delay(10);
            s_shared = tmp + 1;
            rtos_mutex_give(&mtx);
        }
        a_done = 1;
        rtos_task_delete(NULL);
    }

    void task_b(void *p) {
        (void)p;
        for (int i = 0; i < 5; i++) {
            rtos_err_t e = rtos_mutex_take(&mtx, RTOS_WAIT_FOREVER);
            if (e != RTOS_OK) return;
            uint32_t tmp = s_shared;
            rtos_task_delay(15);
            s_shared = tmp + 1;
            rtos_mutex_give(&mtx);
        }
        b_done = 1;
        rtos_task_delete(NULL);
    }

    void bad_giver(void *p) {
        (void)p;
        bad_err = rtos_mutex_give(&mtx);
        rtos_task_delete(NULL);
    }

    rtos_mutex_init(&mtx);

    rtos_task_create(task_a, "a", s_stk0, 160, NULL, 3, NULL);
    rtos_task_create(task_b, "b", s_stk1, 160, NULL, 4, NULL);
    if (!wait_for_val(&a_done, 1, 2000)) { TEST_ASSERT(0, "timeout A"); }
    if (!wait_for_val(&b_done, 1, 2000)) { TEST_ASSERT(0, "timeout B"); }

    TEST_ASSERT(s_shared == 10, "mutex protect: shared should be 10");

    /* verify ownership */
    rtos_err_t e = rtos_mutex_take(&mtx, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_OK, "re-take mutex OK");
    TEST_ASSERT(rtos_mutex_get_holder(&mtx) == rtos_task_get_current(),
                "holder should be self");

    /* non-holder give must be rejected */
    rtos_task_create(bad_giver, "bad", s_stk1, 160, NULL, 5, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(bad_err == RTOS_ERR_STATE, "non-holder give must return ERR_STATE");

    rtos_mutex_give(&mtx);
    TEST_ASSERT(rtos_mutex_get_holder(&mtx) == NULL, "holder should be NULL after give");

    rtos_mutex_delete(&mtx);
    return true;
}

TEST_CASE_REGISTER(mutex_basic, test_mutex_basic);

#endif
