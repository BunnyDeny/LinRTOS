/*
 * Test: Mutex recursive — nested take/give
 * 验证: 递归 take/give 计数、阻塞与唤醒、非持有者 give 拒绝
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_mutex.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_MUTEX_RECURSIVE)

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool wait_bool(volatile bool *f, bool e, uint32_t to)
{
    uint32_t start = rtos_get_tick_count();
    while (*f != e) {
        if ((int32_t)(rtos_get_tick_count() - start) >= (int32_t)to) return false;
        rtos_task_delay(10);
    }
    return true;
}

static bool test_mutex_recursive(void)
{
    static struct rtos_queue mtx;
    static volatile bool t_done;
    static volatile rtos_err_t bad_err;

    t_done = false; bad_err = RTOS_OK;

    void other_task(void *p) {
        (void)p;
        rtos_err_t e = rtos_mutex_take(&mtx, 100);
        if (e != RTOS_OK) { t_done = true; rtos_task_delete(NULL); return; }
        rtos_mutex_give(&mtx);
        t_done = true;
        rtos_task_delete(NULL);
    }

    void bad_giver(void *p) {
        (void)p;
        bad_err = rtos_mutex_give_recursive(&mtx);
        rtos_task_delete(NULL);
    }

    rtos_mutex_init_recursive(&mtx);

    /* 1. take_recursive x3 */
    rtos_err_t e = rtos_mutex_take_recursive(&mtx, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_OK, "take_recursive 1");
    e = rtos_mutex_take_recursive(&mtx, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_OK, "take_recursive 2");
    e = rtos_mutex_take_recursive(&mtx, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_OK, "take_recursive 3");
    TEST_ASSERT(mtx.recursive_count == 3, "count should be 3");
    TEST_ASSERT(rtos_mutex_get_holder(&mtx) == rtos_task_get_current(), "holder should be self");

    /* 2. give_recursive x2 */
    e = rtos_mutex_give_recursive(&mtx);
    TEST_ASSERT(e == RTOS_OK, "give_recursive 1");
    TEST_ASSERT(mtx.recursive_count == 2, "count should be 2");
    e = rtos_mutex_give_recursive(&mtx);
    TEST_ASSERT(e == RTOS_OK, "give_recursive 2");
    TEST_ASSERT(mtx.recursive_count == 1, "count should be 1");
    TEST_ASSERT(rtos_mutex_get_holder(&mtx) == rtos_task_get_current(), "holder still self");

    /* 3. other task tries to take — must block */
    rtos_task_create(other_task, "other", s_stk0, 160, NULL, 5, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(!t_done, "other task should still be blocked");

    /* 4. final give — other task unblocks */
    e = rtos_mutex_give_recursive(&mtx);
    TEST_ASSERT(e == RTOS_OK, "give_recursive 3");
    TEST_ASSERT(mtx.recursive_count == 0, "count should be 0");
    if (!wait_bool(&t_done, true, 500))
        TEST_ASSERT(0, "other task should have run");

    /* 5. non-holder give_recursive must be rejected */
    rtos_mutex_take_recursive(&mtx, RTOS_DONT_WAIT);
    rtos_task_create(bad_giver, "bad", s_stk1, 160, NULL, 3, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(bad_err == RTOS_ERR_STATE, "non-holder give_recursive must return ERR_STATE");
    rtos_mutex_give_recursive(&mtx);

    rtos_mutex_delete(&mtx);
    return true;
}

TEST_CASE_REGISTER(mutex_recursive, test_mutex_recursive);

#endif
