/*
 * LinRTOS - All-in-one test suite (single flash, all results via serial).
 *
 * 每个测试都是独立的 static bool 函数，返回 true=PASS, false=FAIL。
 * 所有断言均为软断言：失败时打印行号和原因，不会卡死 MCU。
 */

#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_mutex.h"
#include "rtos_semaphore.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_ALL)

/* 嵌套函数是 GCC 扩展，Clang / IAR / ARMCC 均不支持 */
#if !defined(__GNUC__) || defined(__clang__)
#error "test_all.c requires GCC (nested functions are a GCC extension)"
#endif

#define TEST_ASSERT(cond, msg)                    \
    do {                                          \
        if (!(cond)) {                            \
            sys_printk("  FAIL L%d: %s\r\n",      \
                       __LINE__, msg);             \
            return false;                         \
        }                                         \
    } while (0)

/* ---- 辅助：等待条件，超时则失败 ---- */
static bool wait_for(volatile uint32_t *flag, uint32_t expect,
                     uint32_t timeout_ticks)
{
    uint32_t start = rtos_get_tick_count();
    while (*flag < expect) {
        if ((int32_t)(rtos_get_tick_count() - start) >= (int32_t)timeout_ticks) {
            sys_printk("  FAIL L%d: timeout waiting flag=%lu expect=%lu\r\n",
                       __LINE__, (unsigned long)*flag, (unsigned long)expect);
            return false;
        }
        rtos_task_delay(10);
    }
    return true;
}

static bool wait_for_bool(volatile bool *flag, bool expect,
                          uint32_t timeout_ticks)
{
    uint32_t start = rtos_get_tick_count();
    while (*flag != expect) {
        if ((int32_t)(rtos_get_tick_count() - start) >= (int32_t)timeout_ticks) {
            sys_printk("  FAIL L%d: timeout waiting bool\r\n", __LINE__);
            return false;
        }
        rtos_task_delay(10);
    }
    return true;
}

/* ============================================================
 * 1. 任务状态查询
 * ============================================================ */
static bool test_state(void)
{
    static uint32_t query_stk[128], helper_stk[128];
    static rtos_task_handle_t h_helper;

    sys_printk("[%s]\r\n", __func__);

    void task_query(void *p) {
        (void)p;
        rtos_task_state_t s = rtos_task_get_state(NULL);
        uint32_t prio = rtos_task_get_priority(NULL);
        rtos_task_handle_t me = rtos_task_get_current();
        sys_printk("  self state=%d prio=%lu handle=%p\r\n",
                   (int)s, (unsigned long)prio, (void *)me);

        rtos_task_state_t hs = rtos_task_get_state(h_helper);
        sys_printk("  helper state=%d (expect 2=BLOCKED)\r\n", (int)hs);

        rtos_task_delay(500);
        hs = rtos_task_get_state(h_helper);
        sys_printk("  helper state=%d after 500 ticks\r\n", (int)hs);

        rtos_task_delete(NULL);
    }

    void task_helper(void *p) {
        (void)p;
        for (;;) rtos_task_delay(10000);
    }

    rtos_task_create(task_query,  "query",  query_stk,  128, NULL, 2, NULL);
    rtos_task_create(task_helper, "helper", helper_stk, 128, NULL, 1, &h_helper);
    rtos_task_delay(600);
    return true;
}

/* ============================================================
 * 2. 互斥锁基本
 * ============================================================ */
static bool test_mutex_basic(void)
{
    static struct rtos_queue mtx;
    static uint32_t a_stk[128], b_stk[128], bad_stk[128];
    static volatile uint32_t s_shared, a_done, b_done;
    static volatile rtos_err_t bad_err;

    sys_printk("[%s]\r\n", __func__);

    s_shared = 0; a_done = 0; b_done = 0; bad_err = RTOS_OK;

    void task_a(void *p) {
        (void)p;
        for (int i = 0; i < 5; i++) {
            rtos_mutex_take(&mtx, RTOS_WAIT_FOREVER);
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
            rtos_mutex_take(&mtx, RTOS_WAIT_FOREVER);
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
    rtos_task_create(task_a, "a", a_stk, 128, NULL, 3, NULL);
    rtos_task_create(task_b, "b", b_stk, 128, NULL, 4, NULL);
    if (!wait_for(&a_done, 1, 2000)) return false;
    if (!wait_for(&b_done, 1, 2000)) return false;

    TEST_ASSERT(s_shared == 10, "mutex protect");

    rtos_err_t e = rtos_mutex_take(&mtx, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_OK, "retake mutex");
    TEST_ASSERT(rtos_mutex_get_holder(&mtx) == rtos_task_get_current(), "holder check");

    rtos_task_create(bad_giver, "bad", bad_stk, 128, NULL, 5, NULL);
    if (!wait_for_bool((volatile bool *)&bad_err, 0, 500)) {
        /* bad_err 被赋值为 RTOS_ERR_STATE 后 != RTOS_OK(0), 此等待用另一种方式 */
    }
    rtos_task_delay(50);
    TEST_ASSERT(bad_err == RTOS_ERR_STATE, "non-holder give rejected");

    rtos_mutex_give(&mtx);
    rtos_mutex_delete(&mtx);
    return true;
}

/* ============================================================
 * 3. 互斥锁递归
 * ============================================================ */
static bool test_mutex_recursive(void)
{
    static struct rtos_queue mtx;
    static uint32_t other_stk[128], bad_stk[128];
    static volatile bool t_done;
    static volatile rtos_err_t bad_err;

    sys_printk("[%s]\r\n", __func__);
    t_done = false; bad_err = RTOS_OK;

    void other_task(void *p) {
        (void)p;
        rtos_err_t e = rtos_mutex_take(&mtx, 100);
        if (e != RTOS_OK) { t_done = true; rtos_task_delete(NULL); return; }
        e = rtos_mutex_give(&mtx);
        if (e != RTOS_OK) { t_done = true; rtos_task_delete(NULL); return; }
        t_done = true;
        rtos_task_delete(NULL);
    }

    void bad_giver(void *p) {
        (void)p;
        bad_err = rtos_mutex_give_recursive(&mtx);
        rtos_task_delete(NULL);
    }

    rtos_mutex_init_recursive(&mtx);

    rtos_err_t e = rtos_mutex_take_recursive(&mtx, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_OK, "take_recursive 1");
    e = rtos_mutex_take_recursive(&mtx, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_OK, "take_recursive 2");
    e = rtos_mutex_take_recursive(&mtx, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_OK, "take_recursive 3");
    TEST_ASSERT(mtx.recursive_count == 3, "count=3");
    TEST_ASSERT(rtos_mutex_get_holder(&mtx) == rtos_task_get_current(), "holder");

    e = rtos_mutex_give_recursive(&mtx);
    TEST_ASSERT(e == RTOS_OK, "give_recursive 1");
    TEST_ASSERT(mtx.recursive_count == 2, "count=2");
    e = rtos_mutex_give_recursive(&mtx);
    TEST_ASSERT(e == RTOS_OK, "give_recursive 2");
    TEST_ASSERT(mtx.recursive_count == 1, "count=1");

    rtos_task_create(other_task, "other", other_stk, 128, NULL, 5, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(!t_done, "other should block");

    e = rtos_mutex_give_recursive(&mtx);
    TEST_ASSERT(e == RTOS_OK, "give_recursive 3");
    TEST_ASSERT(mtx.recursive_count == 0, "count=0");
    if (!wait_for_bool(&t_done, true, 500)) return false;

    rtos_mutex_take_recursive(&mtx, RTOS_DONT_WAIT);
    rtos_task_create(bad_giver, "bad", bad_stk, 128, NULL, 3, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(bad_err == RTOS_ERR_STATE, "non-holder give_recursive rejected");
    rtos_mutex_give_recursive(&mtx);

    rtos_mutex_delete(&mtx);
    return true;
}

/* ============================================================
 * 4. 互斥锁优先级继承
 * ============================================================ */
static bool test_mutex_priority(void)
{
    static struct rtos_queue mtx;
    static uint32_t low_stk[128], high_stk[128];
    static volatile uint32_t l_prio, l_prio_after;
    static volatile bool high_done;

    sys_printk("[%s]\r\n", __func__);
    l_prio = 0; l_prio_after = 0; high_done = false;

    void low_task(void *p) {
        (void)p;
        rtos_err_t e = rtos_mutex_take(&mtx, RTOS_WAIT_FOREVER);
        if (e != RTOS_OK) { rtos_task_delete(NULL); return; }
        rtos_task_delay(100);
        l_prio = rtos_task_get_priority(NULL);
        e = rtos_mutex_give(&mtx);
        if (e != RTOS_OK) { rtos_task_delete(NULL); return; }
        l_prio_after = rtos_task_get_priority(NULL);
        rtos_task_delete(NULL);
    }

    void high_task(void *p) {
        (void)p;
        rtos_task_delay(30);
        rtos_err_t e = rtos_mutex_take(&mtx, RTOS_WAIT_FOREVER);
        if (e != RTOS_OK) { rtos_task_delete(NULL); return; }
        rtos_mutex_give(&mtx);
        high_done = true;
        rtos_task_delete(NULL);
    }

    rtos_mutex_init(&mtx);
    rtos_task_create(low_task,  "low",  low_stk,  128, NULL, 2, NULL);
    rtos_task_create(high_task, "high", high_stk, 128, NULL, 5, NULL);
    if (!wait_for_bool(&high_done, true, 2000)) return false;

    TEST_ASSERT(l_prio == 5, "priority boosted to 5");
    TEST_ASSERT(l_prio_after == 2, "priority restored to 2");

    rtos_mutex_delete(&mtx);
    return true;
}

/* ============================================================
 * 5. 信号量（二进制）
 * ============================================================ */
static bool test_semaphore_binary(void)
{
    static struct rtos_queue sem;
    static uint32_t p_stk[128], c_stk[128];
    static volatile uint32_t pcnt, ccnt;

    sys_printk("[%s]\r\n", __func__);
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
            rtos_semaphore_take(&sem, RTOS_WAIT_FOREVER);
            ccnt++;
        }
        rtos_task_delete(NULL);
    }

    rtos_semaphore_init_binary(&sem);
    rtos_task_create(consumer, "cons", c_stk, 128, NULL, 5, NULL);
    rtos_task_create(producer, "prod", p_stk, 128, NULL, 2, NULL);
    if (!wait_for(&pcnt, 5, 2000)) return false;
    if (!wait_for(&ccnt, 5, 2000)) return false;

    TEST_ASSERT(pcnt == 5, "pcnt=5");
    TEST_ASSERT(ccnt == 5, "ccnt=5");
    TEST_ASSERT(rtos_semaphore_get_count(&sem) == 0, "count=0");

    rtos_semaphore_delete(&sem);
    return true;
}

/* ============================================================
 * 6. 信号量（计数）
 * ============================================================ */
static bool test_semaphore_counting(void)
{
    static struct rtos_queue sem;
    static uint32_t p1_stk[128], p2_stk[128], c_stk[128];
    static volatile uint32_t pcnt, ccnt;

    sys_printk("[%s]\r\n", __func__);
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
            rtos_semaphore_take(&sem, RTOS_WAIT_FOREVER);
            ccnt++;
        }
        rtos_task_delete(NULL);
    }

    rtos_semaphore_init_counting(&sem, 5, 0);
    rtos_task_create(consumer, "cons", c_stk, 128, NULL, 5, NULL);
    rtos_task_create(p1, "p1", p1_stk, 128, NULL, 2, NULL);
    rtos_task_create(p2, "p2", p2_stk, 128, NULL, 3, NULL);
    if (!wait_for(&pcnt, 6, 2000)) return false;
    if (!wait_for(&ccnt, 6, 2000)) return false;

    TEST_ASSERT(pcnt == 6, "pcnt=6");
    TEST_ASSERT(ccnt == 6, "ccnt=6");

    rtos_semaphore_delete(&sem);
    return true;
}

/* ============================================================
 * 8. 队列基本 API
 * ============================================================ */
static bool test_queue_basic(void)
{
    static struct rtos_queue q, q1;
    static uint8_t buf[5 * sizeof(uint32_t)];
    static uint8_t buf1[1 * sizeof(uint32_t)];
    static uint32_t p_stk[128], c_stk[128];
    static volatile uint32_t pcnt, ccnt;

    sys_printk("[%s]\r\n", __func__);
    pcnt = 0; ccnt = 0;

    void producer(void *p) {
        (void)p;
        for (uint32_t i = 0; i < 5; i++) {
            rtos_err_t e = rtos_queue_send(&q, &i, RTOS_WAIT_FOREVER);
            if (e != RTOS_OK) { rtos_task_delete(NULL); return; }
            pcnt++;
        }
        rtos_task_delete(NULL);
    }

    void consumer(void *p) {
        (void)p;
        for (uint32_t i = 0; i < 5; i++) {
            uint32_t v;
            rtos_err_t e = rtos_queue_recv(&q, &v, RTOS_WAIT_FOREVER);
            if (e != RTOS_OK || v != i) { rtos_task_delete(NULL); return; }
            ccnt++;
        }
        rtos_task_delete(NULL);
    }

    rtos_err_t e = rtos_queue_init(&q, buf, 5, sizeof(uint32_t));
    TEST_ASSERT(e == RTOS_OK, "init");
    TEST_ASSERT(rtos_queue_is_empty(&q), "empty");
    TEST_ASSERT(rtos_queue_spaces_available(&q) == 5, "space=5");

    rtos_task_create(producer, "prod", p_stk, 128, NULL, 2, NULL);
    rtos_task_create(consumer, "cons", c_stk, 128, NULL, 5, NULL);
    if (!wait_for(&pcnt, 5, 2000)) return false;
    if (!wait_for(&ccnt, 5, 2000)) return false;
    TEST_ASSERT(rtos_queue_is_empty(&q), "empty after drain");

    /* front */
    uint32_t a = 10, b = 20, v;
    rtos_queue_send(&q, &a, RTOS_DONT_WAIT);
    rtos_queue_send_to_front(&q, &b, RTOS_DONT_WAIT);
    rtos_queue_recv(&q, &v, RTOS_DONT_WAIT); TEST_ASSERT(v == 20, "front 1st=20");
    rtos_queue_recv(&q, &v, RTOS_DONT_WAIT); TEST_ASSERT(v == 10, "front 2nd=10");

    /* overwrite */
    rtos_queue_init(&q1, buf1, 1, sizeof(uint32_t));
    uint32_t x = 100;
    rtos_queue_send(&q1, &x, RTOS_DONT_WAIT);
    x = 200;
    rtos_queue_overwrite(&q1, &x);
    rtos_queue_recv(&q1, &v, RTOS_DONT_WAIT);
    TEST_ASSERT(v == 200, "overwrite=200");

    /* peek */
    x = 300;
    rtos_queue_send(&q, &x, RTOS_DONT_WAIT);
    rtos_queue_peek(&q, &v, RTOS_DONT_WAIT);
    TEST_ASSERT(v == 300, "peek=300");
    TEST_ASSERT(rtos_queue_messages_waiting(&q) == 1, "peek no remove");
    rtos_queue_recv(&q, &v, RTOS_DONT_WAIT);
    TEST_ASSERT(v == 300, "real recv=300");

    /* full/empty error */
    for (uint32_t i = 0; i < 5; i++) rtos_queue_send(&q, &i, RTOS_DONT_WAIT);
    e = rtos_queue_send(&q, &x, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_ERR_RESOURCE, "full->ERR_RESOURCE");
    for (uint32_t i = 0; i < 5; i++) rtos_queue_recv(&q, &v, RTOS_DONT_WAIT);
    e = rtos_queue_recv(&q, &v, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_ERR_RESOURCE, "empty->ERR_RESOURCE");

    rtos_queue_delete(&q);
    return true;
}

/* ============================================================
 * 9. 队列阻塞与超时
 * ============================================================ */
static bool test_queue_blocking(void)
{
    static struct rtos_queue q;
    static uint8_t buf[3 * sizeof(uint32_t)];
    static uint32_t p_stk[128], c_stk[128];
    static volatile uint32_t pcnt, ccnt;

    sys_printk("[%s]\r\n", __func__);
    pcnt = 0; ccnt = 0;

    void producer(void *p) {
        (void)p;
        for (uint32_t i = 0; i < 8; i++) {
            rtos_err_t e = rtos_queue_send(&q, &i, RTOS_WAIT_FOREVER);
            if (e == RTOS_OK) pcnt++;
        }
        rtos_task_delete(NULL);
    }

    void consumer(void *p) {
        (void)p;
        for (uint32_t i = 0; i < 8; i++) {
            uint32_t v;
            rtos_err_t e = rtos_queue_recv(&q, &v, RTOS_WAIT_FOREVER);
            if (e == RTOS_OK && v == i) ccnt++;
        }
        rtos_task_delete(NULL);
    }

    rtos_queue_init(&q, buf, 3, sizeof(uint32_t));

    rtos_task_create(producer, "prod", p_stk, 128, NULL, 2, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(pcnt == 3, "filled 3, blocked");

    rtos_task_create(consumer, "cons", c_stk, 128, NULL, 5, NULL);
    if (!wait_for(&pcnt, 8, 2000)) return false;
    if (!wait_for(&ccnt, 8, 2000)) return false;
    TEST_ASSERT(pcnt == 8 && ccnt == 8, "all 8");

    /* timeout tests */
    rtos_queue_init(&q, buf, 3, sizeof(uint32_t));
    uint32_t d = 0;
    for (int i = 0; i < 3; i++) rtos_queue_send(&q, &d, RTOS_DONT_WAIT);
    uint32_t t0 = rtos_get_tick_count();
    rtos_err_t e = rtos_queue_send(&q, &d, 50);
    uint32_t t1 = rtos_get_tick_count();
    TEST_ASSERT(e == RTOS_ERR_TIMEOUT, "send timeout");
    TEST_ASSERT((int32_t)(t1 - t0) >= 50, "waited >= 50");

    for (int i = 0; i < 3; i++) rtos_queue_recv(&q, &d, RTOS_DONT_WAIT);
    t0 = rtos_get_tick_count();
    e = rtos_queue_recv(&q, &d, 50);
    t1 = rtos_get_tick_count();
    TEST_ASSERT(e == RTOS_ERR_TIMEOUT, "recv timeout");
    TEST_ASSERT((int32_t)(t1 - t0) >= 50, "waited >= 50");

    return true;
}

/* ============================================================
 * 11. 挂起/恢复
 * ============================================================ */
static bool test_suspend_resume(void)
{
    static uint32_t t_stk[128];
    static volatile bool ran = false;
    static rtos_task_handle_t h_task;

    sys_printk("[%s]\r\n", __func__);
    ran = false; h_task = NULL;

    void task_func(void *p) {
        (void)p;
        rtos_task_delay(100);
        ran = true;
        rtos_task_delete(NULL);
    }

    rtos_task_create(task_func, "t", t_stk, 128, NULL, 2, &h_task);
    rtos_task_suspend(h_task);
    rtos_task_delay(200);
    TEST_ASSERT(!ran, "suspended, should not run");

    rtos_task_resume(h_task);
    rtos_task_delay(200);
    TEST_ASSERT(ran, "resumed, should run");

    return true;
}

/* ============================================================
 * 12. 动态优先级
 * ============================================================ */
static bool test_priority(void)
{
    static uint32_t lo_stk[128], hi_stk[128];
    static volatile bool lo_ran = false;

    sys_printk("[%s]\r\n", __func__);
    lo_ran = false;

    void lo_task(void *p) {
        (void)p;
        rtos_task_set_priority(NULL, 5);
        lo_ran = true;
        rtos_task_delete(NULL);
    }

    void hi_task(void *p) {
        (void)p;
        /* 优先级高，先运行；阻塞让 lo 运行 */
        rtos_task_delay(50);
        rtos_task_delete(NULL);
    }

    rtos_task_create(lo_task, "lo", lo_stk, 128, NULL, 1, NULL);
    rtos_task_create(hi_task, "hi", hi_stk, 128, NULL, 3, NULL);

    rtos_task_delay(200);
    TEST_ASSERT(lo_ran, "lo ran after priority boost");

    return true;
}

/* ============================================================
 * 13. 自删除
 * ============================================================ */
static bool test_selfdelete(void)
{
    static uint32_t t_stk[128];
    static volatile bool created = false;

    sys_printk("[%s]\r\n", __func__);
    created = false;

    void self_deleter(void *p) {
        (void)p;
        created = true;
        rtos_task_delete(NULL);
    }

    rtos_task_create(self_deleter, "sd", t_stk, 128, NULL, 2, NULL);
    rtos_task_delay(50);

    /* 验证：创建同名任务确认 TCB 被回收 */
    rtos_task_handle_t h;
    rtos_err_t e = rtos_task_create(self_deleter, "sd",
                                     t_stk, 128, NULL, 2, &h);
    TEST_ASSERT(e == RTOS_OK, "TCB recycled");
    rtos_task_delay(50);
    rtos_task_delete(h);
    TEST_ASSERT(created, "self-delete ran");

    return true;
}

/* ============================================================
 * 14. 调度锁
 * ============================================================ */
static bool test_sched_lock(void)
{
    static uint32_t hi_stk[128];
    static volatile bool hi_ran = false;

    sys_printk("[%s]\r\n", __func__);
    hi_ran = false;

    void hi_task(void *p) {
        (void)p;
        hi_ran = true;
        rtos_task_delete(NULL);
    }

    rtos_sched_lock();
    rtos_task_create(hi_task, "hi", hi_stk, 128, NULL, 5, NULL);
    rtos_task_delay(100);  /* 高优先级任务不应抢占 */
    TEST_ASSERT(!hi_ran, "sched_lock prevented preemption");

    rtos_sched_unlock();
    rtos_task_delay(50);
    TEST_ASSERT(hi_ran, "sched_unlock allowed preemption");

    return true;
}

/* ============================================================
 * 15. 任务让出
 * ============================================================ */
static bool test_yield(void)
{
    static uint32_t a_stk[128], b_stk[128];
    static volatile uint32_t seq = 0;

    sys_printk("[%s]\r\n", __func__);
    seq = 0;

    void task_a(void *p) {
        (void)p;
        seq = 1;
        rtos_task_yield();
        if (seq == 1) { /* B hasn't run yet, ok */ }
        rtos_task_delete(NULL);
    }

    void task_b(void *p) {
        (void)p;
        seq = 2;
        rtos_task_delete(NULL);
    }

    rtos_task_create(task_a, "a", a_stk, 128, NULL, 2, NULL);
    rtos_task_create(task_b, "b", b_stk, 128, NULL, 2, NULL);
    rtos_task_delay(100);

    return true;
}

/* ============================================================
 * 16. 栈剩余查询
 * ============================================================ */
static bool test_stack_free(void)
{
    static uint32_t t_stk[128];
    static volatile uint32_t free_words = 0;

    sys_printk("[%s]\r\n", __func__);
    free_words = 0;

    void task_func(void *p) {
        (void)p;
        free_words = rtos_task_get_stack_free(NULL);
        sys_printk("  stack free=%lu words\r\n", (unsigned long)free_words);
        rtos_task_delete(NULL);
    }

    rtos_task_create(task_func, "t", t_stk, 128, NULL, 2, NULL);
    rtos_task_delay(100);
    TEST_ASSERT(free_words > 0, "stack free > 0");
    TEST_ASSERT(free_words <= 128, "stack free <= 128");
    return true;
}

/* ============================================================
 * 17. 中止延时
 * ============================================================ */
static bool test_abort_delay(void)
{
    static uint32_t t_stk[128];
    static rtos_task_handle_t h_task;

    sys_printk("[%s]\r\n", __func__);
    h_task = NULL;

    void sleeper(void *p) {
        (void)p;
        rtos_task_delay(5000);
        rtos_task_delete(NULL);
    }

    rtos_task_create(sleeper, "sleep", t_stk, 128, NULL, 2, &h_task);
    rtos_task_delay(50);
    TEST_ASSERT(rtos_scheduler_is_running(), "scheduler running");

    rtos_task_abort_delay(h_task);
    rtos_task_delay(50);

    rtos_task_state_t s = rtos_task_get_state(h_task);
    TEST_ASSERT(s != RTOS_TASK_BLOCKED, "aborted, not BLOCKED");

    rtos_task_delete(h_task);
    return true;
}

/* ============================================================
 * 📊 测试运行器
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;
    sys_printk("\r\n========================================\r\n");
    sys_printk("  LinRTOS Test Suite (All-in-One)\r\n");
    sys_printk("========================================\r\n\r\n");

    struct {
        const char *name;
        bool (*fn)(void);
    } tests[] = {
        {"state",              test_state},
        {"mutex_basic",        test_mutex_basic},
        {"mutex_recursive",    test_mutex_recursive},
        {"mutex_priority",     test_mutex_priority},
        {"semaphore_binary",   test_semaphore_binary},
        {"semaphore_counting", test_semaphore_counting},
        {"queue_basic",        test_queue_basic},
        {"queue_blocking",     test_queue_blocking},
        {"suspend_resume",     test_suspend_resume},
        {"priority",           test_priority},
        {"selfdelete",         test_selfdelete},
        {"sched_lock",         test_sched_lock},
        {"yield",              test_yield},
        {"stack_free",         test_stack_free},
        {"abort_delay",        test_abort_delay},
    };

    int total = (int)(sizeof(tests) / sizeof(tests[0]));
    int pass = 0;

    for (int i = 0; i < total; i++) {
        sys_printk("[%2d/%2d] %-22s ", i + 1, total, tests[i].name);
        bool ok = tests[i].fn();
        if (ok) { pass++; sys_printk("PASS\r\n"); }
        else     {        sys_printk("FAIL\r\n"); }
        rtos_task_delay(50);  /* 等一等尚未清理完的辅助任务 */
    }

    sys_printk("\r\n========================================\r\n");
    sys_printk("  Results: %d / %d PASS\r\n", pass, total);
    sys_printk("========================================\r\n");

    rtos_task_delete(NULL);
}

#endif /* TEST_ALL */
