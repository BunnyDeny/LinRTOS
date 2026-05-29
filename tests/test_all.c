/*
 * LinRTOS - All-in-one test suite (single flash, all results via serial).
 *
 * 每个测试都是独立的 static bool 函数，返回 true=PASS, false=FAIL。
 * 所有断言均为软断言：失败时打印行号和原因，不会卡死 MCU。
 *
 * ISR 测试（queue_isr, semaphore_isr）通过统一的 SysTick_Handler
 * 分发，使用 active flag 隔离，各测试串行运行互不干扰。
 *
 * FPU / CmBacktrace / Workqueue 测试通过条件编译按需启用。
 */

#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_mutex.h"
#include "rtos_semaphore.h"

#ifdef ARCH_ENABLE_FPU
#include <math.h>
#endif

#ifdef COMPONENT_CM_BACKTRACE
#include "cm_backtrace.h"
#endif

#ifdef WORKQUEUE
#include "workqueue.h"
#endif

#if defined(ENABLE_TEST_CASES) && defined(TEST_ALL)

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
 * ISR 测试共享基础设施
 *
 * ISR 测试共用唯一的 SysTick_Handler。每个 ISR 测试运行前
 * 设置自己的 active flag，运行完毕后清除，保证互不干扰。
 * ============================================================ */

static volatile bool s_isr_queue_active = false;
static volatile bool s_isr_sem_active   = false;
static volatile bool s_suite_done       = false;

static struct rtos_queue s_isr_q;
static uint8_t  s_isr_q_buf[5 * sizeof(uint32_t)];
static volatile uint32_t s_isr_q_sent  = 0;
static volatile bool     s_isr_q_done  = false;
static volatile bool     s_isr_q_woken = false;

static struct rtos_queue s_isr_sem;
static volatile uint32_t s_isr_sem_sent  = 0;
static volatile bool     s_isr_sem_done  = false;
static volatile bool     s_isr_sem_woken = false;

/* ---- 共享任务栈池（测试串行运行，可安全复用）---- */
static uint32_t s_stk0[160];
static uint32_t s_stk1[160];
static uint32_t s_stk2[160];
static uint32_t __attribute__((unused)) s_stk3[160];
#ifdef ARCH_ENABLE_FPU
static uint32_t s_fpu_stk0[256];
static uint32_t s_fpu_stk1[256];
static uint32_t s_fpu_stk2[160];
#endif

void SysTick_Handler(void)
{
    rtos_tick_handler();

    if (s_suite_done) {
        return;
    }

#ifdef WORKQUEUE
    if (system_wq) {
        workqueue_tick_handler(system_wq, rtos_get_tick_count());
        workqueue_run_one(system_wq);
    }
#endif

    static uint32_t isr_cnt = 0;
    isr_cnt++;

    if (s_isr_queue_active && (isr_cnt % 200 == 0) && !s_isr_q_done) {
        uint32_t d = isr_cnt;
        bool hp = false;
        if (rtos_queue_generic_send_from_isr(&s_isr_q, &d,
                                              RTOS_QUEUE_SEND_BACK,
                                              &hp) == RTOS_OK) {
            s_isr_q_sent++;
            if (hp) s_isr_q_woken = true;
        }
    }

    if (s_isr_sem_active && (isr_cnt % 200 == 0) && !s_isr_sem_done) {
        bool hp = false;
        if (rtos_semaphore_give_from_isr(&s_isr_sem, &hp) == RTOS_OK) {
            s_isr_sem_sent++;
            if (hp) s_isr_sem_woken = true;
        }
    }
}

/* ============================================================
 * 1. 任务状态查询
 * ============================================================ */
/* task_helper 永久存活，不能用共享栈（后续测试会覆写），须独立栈 */
static uint32_t s_helper_stk[160];

static bool test_state(void)
{
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

    rtos_task_create(task_query,  "query",  s_stk0, 160, NULL, 2, NULL);
    rtos_task_create(task_helper, "helper", s_helper_stk, 160, NULL, 1, &h_helper);
    rtos_task_delay(600);
    return true;
}

/* ============================================================
 * 2. 互斥锁基本
 * ============================================================ */
static bool test_mutex_basic(void)
{
    static struct rtos_queue mtx;
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
    rtos_task_create(task_a, "a", s_stk0, 160, NULL, 3, NULL);
    rtos_task_create(task_b, "b", s_stk1, 160, NULL, 4, NULL);
    if (!wait_for(&a_done, 1, 2000)) return false;
    if (!wait_for(&b_done, 1, 2000)) return false;

    TEST_ASSERT(s_shared == 10, "mutex protect");

    rtos_err_t e = rtos_mutex_take(&mtx, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_OK, "retake mutex");
    TEST_ASSERT(rtos_mutex_get_holder(&mtx) == rtos_task_get_current(), "holder check");

    rtos_task_create(bad_giver, "bad", s_stk2, 160, NULL, 5, NULL);
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

    rtos_task_create(other_task, "other", s_stk0, 160, NULL, 5, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(!t_done, "other should block");

    e = rtos_mutex_give_recursive(&mtx);
    TEST_ASSERT(e == RTOS_OK, "give_recursive 3");
    TEST_ASSERT(mtx.recursive_count == 0, "count=0");
    if (!wait_for_bool(&t_done, true, 500)) return false;

    rtos_mutex_take_recursive(&mtx, RTOS_DONT_WAIT);
    rtos_task_create(bad_giver, "bad", s_stk2, 160, NULL, 3, NULL);
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
    rtos_task_create(low_task,  "low",  s_stk0, 160, NULL, 2, NULL);
    rtos_task_create(high_task, "high", s_stk1, 160, NULL, 5, NULL);
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
    rtos_task_create(consumer, "cons", s_stk1, 160, NULL, 5, NULL);
    rtos_task_create(producer, "prod", s_stk0, 160, NULL, 2, NULL);
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
    rtos_task_create(consumer, "cons", s_stk2, 160, NULL, 5, NULL);
    rtos_task_create(p1, "p1", s_stk0, 160, NULL, 2, NULL);
    rtos_task_create(p2, "p2", s_stk1, 160, NULL, 3, NULL);
    if (!wait_for(&pcnt, 6, 2000)) return false;
    if (!wait_for(&ccnt, 6, 2000)) return false;

    TEST_ASSERT(pcnt == 6, "pcnt=6");
    TEST_ASSERT(ccnt == 6, "ccnt=6");

    rtos_semaphore_delete(&sem);
    return true;
}

/* ============================================================
 * 7. 队列基本 API
 * ============================================================ */
static bool test_queue_basic(void)
{
    static struct rtos_queue q, q1;
    static uint8_t buf[5 * sizeof(uint32_t)];
    static uint8_t buf1[1 * sizeof(uint32_t)];
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
    TEST_ASSERT(rtos_queue_is_full(&q) == false, "not full");

    rtos_task_create(producer, "prod", s_stk0, 160, NULL, 2, NULL);
    rtos_task_create(consumer, "cons", s_stk1, 160, NULL, 5, NULL);
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
    TEST_ASSERT(rtos_queue_is_full(&q), "is_full");
    e = rtos_queue_send(&q, &x, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_ERR_RESOURCE, "full->ERR_RESOURCE");
    for (uint32_t i = 0; i < 5; i++) rtos_queue_recv(&q, &v, RTOS_DONT_WAIT);
    e = rtos_queue_recv(&q, &v, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_ERR_RESOURCE, "empty->ERR_RESOURCE");

    rtos_queue_delete(&q);
    return true;
}

/* ============================================================
 * 8. 队列阻塞与超时
 * ============================================================ */
static bool test_queue_blocking(void)
{
    static struct rtos_queue q;
    static uint8_t buf[3 * sizeof(uint32_t)];
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

    rtos_task_create(producer, "prod", s_stk0, 160, NULL, 2, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(pcnt == 3, "filled 3, blocked");

    rtos_task_create(consumer, "cons", s_stk1, 160, NULL, 5, NULL);
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
 * 9. 挂起/恢复（外部挂起）
 * ============================================================ */
static bool test_suspend_resume(void)
{
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

    rtos_task_create(task_func, "t", s_stk0, 160, NULL, 2, &h_task);
    rtos_task_suspend(h_task);
    rtos_task_delay(200);
    TEST_ASSERT(!ran, "suspended, should not run");

    rtos_task_resume(h_task);
    rtos_task_delay(200);
    TEST_ASSERT(ran, "resumed, should run");

    return true;
}

/* ============================================================
 * 10. 自挂起 suspend(NULL)
 * ============================================================ */
static bool test_self_suspend(void)
{
    static volatile uint32_t sscnt = 0;
    static volatile bool ctrl_done = false;
    static rtos_task_handle_t h_self;

    sys_printk("[%s]\r\n", __func__);
    sscnt = 0; ctrl_done = false; h_self = NULL;

    void self_task(void *p) {
        (void)p;
        sscnt = 1;
        rtos_task_delay(50);
        sscnt = 2;
        rtos_task_suspend(NULL);
        /* 外部恢复后继续执行 */
        sscnt = 3;
        rtos_task_delete(NULL);
    }

    void ctrl_task(void *p) {
        (void)p;
        uint32_t start = rtos_get_tick_count();
        while (sscnt < 2) {
            if ((int32_t)(rtos_get_tick_count() - start) > 1000) return;
            rtos_task_delay(10);
        }
        /* 确认已自挂起 */
        rtos_task_delay(50);
        if (sscnt != 2) { ctrl_done = true; rtos_task_delete(NULL); return; }

        /* 恢复 */
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

    TEST_ASSERT(ctrl_done, "ctrl completed");
    TEST_ASSERT(sscnt >= 3, "self resumed, sscnt>=3");

    return true;
}

/* ============================================================
 * 11. 动态优先级
 * ============================================================ */
static bool test_priority(void)
{
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
        rtos_task_delay(50);
        rtos_task_delete(NULL);
    }

    rtos_task_create(lo_task, "lo", s_stk0, 160, NULL, 1, NULL);
    rtos_task_create(hi_task, "hi", s_stk1, 160, NULL, 3, NULL);

    rtos_task_delay(200);
    TEST_ASSERT(lo_ran, "lo ran after priority boost");

    return true;
}

/* ============================================================
 * 12. 自删除
 * ============================================================ */
static bool test_selfdelete(void)
{
    static volatile bool created = false;

    sys_printk("[%s]\r\n", __func__);
    created = false;

    void self_deleter(void *p) {
        (void)p;
        created = true;
        rtos_task_delete(NULL);
    }

    rtos_task_create(self_deleter, "sd", s_stk0, 160, NULL, 2, NULL);
    rtos_task_delay(50);

    rtos_task_handle_t h;
    rtos_err_t e = rtos_task_create(self_deleter, "sd",
                                     s_stk0, 160, NULL, 2, &h);
    TEST_ASSERT(e == RTOS_OK, "TCB recycled");
    rtos_task_delay(50);
    rtos_task_delete(h);
    TEST_ASSERT(created, "self-delete ran");

    return true;
}

/* ============================================================
 * 13. 调度锁（含嵌套锁）
 * ============================================================ */
static bool test_sched_lock(void)
{
    static volatile bool hi_ran = false;

    sys_printk("[%s]\r\n", __func__);

    void hi_task(void *p) {
        (void)p;
        hi_ran = true;
        rtos_task_delete(NULL);
    }

    /* ---- 单级锁 ---- */
    hi_ran = false;
    rtos_sched_lock();
    rtos_task_create(hi_task, "hi", s_stk0, 160, NULL, 5, NULL);
    rtos_task_delay(100);
    TEST_ASSERT(!hi_ran, "single lock prevented preemption");

    rtos_sched_unlock();
    rtos_task_delay(50);
    TEST_ASSERT(hi_ran, "single unlock allowed preemption");

    /* ---- 嵌套锁（3 层） ---- */
    hi_ran = false;
    rtos_sched_lock();
    rtos_sched_lock();
    rtos_sched_lock();
    rtos_task_create(hi_task, "hi2", s_stk0, 160, NULL, 5, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(!hi_ran, "nested 3: no preempt after lock3");

    rtos_sched_unlock();  /* 3 -> 2 */
    rtos_task_delay(50);
    TEST_ASSERT(!hi_ran, "nested: no preempt after unlock to 2");

    rtos_sched_unlock();  /* 2 -> 1 */
    rtos_task_delay(50);
    TEST_ASSERT(!hi_ran, "nested: no preempt after unlock to 1");

    rtos_sched_unlock();  /* 1 -> 0 */
    rtos_task_delay(50);
    TEST_ASSERT(hi_ran, "nested: preempt after unlock to 0");

    return true;
}

/* ============================================================
 * 14. 任务让出
 * ============================================================ */
static bool test_yield(void)
{
    sys_printk("[%s]\r\n", __func__);

    void task_a(void *p) {
        (void)p;
        rtos_task_yield();
        rtos_task_delete(NULL);
    }

    void task_b(void *p) {
        (void)p;
        rtos_task_delete(NULL);
    }

    rtos_task_create(task_a, "a", s_stk0, 160, NULL, 2, NULL);
    rtos_task_create(task_b, "b", s_stk1, 160, NULL, 2, NULL);
    rtos_task_delay(100);

    return true;
}

/* ============================================================
 * 15. 绝对周期延时 delay_until
 * ============================================================ */
static bool test_delay_until(void)
{
    static volatile uint32_t du_count = 0;

    sys_printk("[%s]\r\n", __func__);
    du_count = 0;

    void periodic(void *p) {
        (void)p;
        uint32_t prev = rtos_get_tick_count();
        for (int i = 0; i < 5; i++) {
            du_count++;
            rtos_task_delay_until(&prev, 100);
        }
        rtos_task_delete(NULL);
    }

    rtos_task_create(periodic, "per", s_stk0, 160, NULL, 3, NULL);
    if (!wait_for(&du_count, 5, 2000)) return false;
    TEST_ASSERT(du_count == 5, "delay_until 5 cycles");

    /* periodic 最后一次 delay_until 可能尚未到期，
     * 必须等它自删完成，否则下一测试复用 s_stk0 会覆盖其栈 */
    rtos_task_delay(120);

    return true;
}

/* ============================================================
 * 16. 栈剩余查询
 * ============================================================ */
static bool test_stack_free(void)
{
    static volatile uint32_t free_words = 0;

    sys_printk("[%s]\r\n", __func__);
    free_words = 0;

    void task_func(void *p) {
        (void)p;
        free_words = rtos_task_get_stack_free(NULL);
        sys_printk("  stack free=%lu words\r\n", (unsigned long)free_words);
        rtos_task_delete(NULL);
    }

    rtos_task_create(task_func, "t", s_stk0, 160, NULL, 2, NULL);
    rtos_task_delay(100);
    TEST_ASSERT(free_words > 0, "stack free > 0");
    TEST_ASSERT(free_words <= 160, "stack free <= 160");
    return true;
}

/* ============================================================
 * 17. 中止延时
 * ============================================================ */
static bool test_abort_delay(void)
{
    static rtos_task_handle_t h_task;

    sys_printk("[%s]\r\n", __func__);
    h_task = NULL;

    void sleeper(void *p) {
        (void)p;
        rtos_task_delay(5000);
        rtos_task_delete(NULL);
    }

    rtos_task_create(sleeper, "sleep", s_stk0, 160, NULL, 2, &h_task);
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
 * 18. 队列 ISR（send_from_isr → 任务消费）
 * ============================================================ */
static bool test_queue_isr(void)
{
    static volatile bool cons_done = false;

    sys_printk("[%s]\r\n", __func__);

    s_isr_q_sent  = 0;
    s_isr_q_done  = false;
    s_isr_q_woken = false;
    cons_done     = false;

    void consumer(void *p) {
        (void)p;
        for (uint32_t i = 0; i < 8; i++) {
            uint32_t v;
            if (rtos_queue_recv(&s_isr_q, &v, RTOS_WAIT_FOREVER) == RTOS_OK) {
                sys_printk("  q-isr recv %lu\r\n", (unsigned long)v);
            }
        }
        s_isr_q_done = true;
        cons_done = true;
        rtos_task_delete(NULL);
    }

    rtos_queue_init(&s_isr_q, s_isr_q_buf, 5, sizeof(uint32_t));
    rtos_task_create(consumer, "qcons", s_stk0, 160, NULL, 5, NULL);

    s_isr_queue_active = true;
    while (!cons_done) {
        rtos_task_delay(200);
        if (s_isr_q_sent >= 12 && !cons_done) break; /* 安全超时 */
    }
    s_isr_queue_active = false;

    TEST_ASSERT(cons_done, "consumer received all 8");
    TEST_ASSERT(s_isr_q_sent >= 8, "isr sent >= 8");
    TEST_ASSERT(s_isr_q_woken, "isr woke a task");

    rtos_queue_delete(&s_isr_q);
    return true;
}

/* ============================================================
 * 19. 信号量 ISR（give_from_isr → 任务 take）
 * ============================================================ */
static bool test_semaphore_isr(void)
{
    static volatile bool cons_done = false;

    sys_printk("[%s]\r\n", __func__);

    s_isr_sem_sent  = 0;
    s_isr_sem_done  = false;
    s_isr_sem_woken = false;
    cons_done       = false;

    void consumer(void *p) {
        (void)p;
        for (uint32_t i = 0; i < 5; i++) {
            rtos_err_t e = rtos_semaphore_take(&s_isr_sem, RTOS_WAIT_FOREVER);
            if (e == RTOS_OK) {
                sys_printk("  sem-isr take %lu\r\n", (unsigned long)(i + 1));
            }
        }
        s_isr_sem_done = true;
        cons_done = true;
        rtos_task_delete(NULL);
    }

    rtos_semaphore_init_binary(&s_isr_sem);
    rtos_task_create(consumer, "scons", s_stk0, 160, NULL, 5, NULL);

    s_isr_sem_active = true;
    while (!cons_done) {
        rtos_task_delay(200);
        if (s_isr_sem_sent >= 10 && !cons_done) break;
    }
    s_isr_sem_active = false;

    TEST_ASSERT(cons_done, "consumer received all 5");
    TEST_ASSERT(s_isr_sem_sent >= 5, "isr sent >= 5");
    TEST_ASSERT(s_isr_sem_woken, "isr woke a task");

    rtos_semaphore_delete(&s_isr_sem);
    return true;
}

/* ============================================================
 * 20. FPU 上下文切换（条件：ARCH_ENABLE_FPU）
 * ============================================================ */
#ifdef ARCH_ENABLE_FPU
static bool test_fpu(void)
{
    static volatile uint32_t fp_cycles = 0;
    static volatile float fp_result_a = 0.0f;
    static volatile float fp_result_b = 0.0f;

    sys_printk("[%s]\r\n", __func__);
    fp_cycles = 0; fp_result_a = 0.0f; fp_result_b = 0.0f;

    void fp_task_a(void *p) {
        (void)p;
        float s = 0.0f;
        for (int i = 0; i < 10; i++) {
            s += 0.1f;
            (void)sinf(s);
            fp_cycles++;
            rtos_task_delay(20);
        }
        fp_result_a = s;  /* 期望 = 1.0f */
        rtos_task_delete(NULL);
    }

    void fp_task_b(void *p) {
        (void)p;
        float v = 1.0f;
        for (int i = 0; i < 10; i++) {
            v = v * 1.5f + 0.1f;
            fp_cycles++;
            rtos_task_delay(25);
        }
        fp_result_b = v;
        rtos_task_delete(NULL);
    }

    void intruder(void *p) {
        (void)p;
        for (int i = 0; i < 15; i++) {
            volatile float x = 0.5f;
            (void)x;
            rtos_task_delay(15);
        }
        rtos_task_delete(NULL);
    }

    rtos_task_create(fp_task_a, "fpa", s_fpu_stk0, 256, NULL, 2, NULL);
    rtos_task_create(fp_task_b, "fpb", s_fpu_stk1, 256, NULL, 1, NULL);
    rtos_task_create(intruder,  "intr", s_fpu_stk2, 160, NULL, 3, NULL);

    rtos_task_delay(600);

    TEST_ASSERT(fp_cycles >= 15, "fp tasks made progress");

    float diff = fp_result_a - 1.0f;
    if (diff < 0.0f) diff = -diff;
    TEST_ASSERT(diff < 0.01f, "fp_task_a accuracy");
    TEST_ASSERT(fp_result_b > 1.0f, "fp_task_b made progress");

    return true;
}
#else
static bool test_fpu(void)
{
    sys_printk("[%s] SKIP (no FPU)\r\n", __func__);
    return true;
}
#endif

/* ============================================================
 * 21. CmBacktrace（条件：COMPONENT_CM_BACKTRACE）
 * ============================================================ */
#ifdef COMPONENT_CM_BACKTRACE
static bool test_cm_backtrace(void)
{
    sys_printk("[%s]\r\n", __func__);

    cm_backtrace_init("LinRTOS-test_all", "v1.0", "v1.0");
    cm_backtrace_firmware_info();

    uint32_t call_stack[16] = {0};
    size_t depth;
    uint32_t sp = cmb_get_sp();

    depth = cm_backtrace_call_stack(call_stack,
                                     sizeof(call_stack) / sizeof(call_stack[0]),
                                     sp);
    sys_printk("  call stack depth=%u\r\n", (unsigned)depth);
    for (size_t i = 0; i < depth; i++) {
        sys_printk("  [%u] 0x%08X\r\n", (unsigned)i, (unsigned)call_stack[i]);
    }

    TEST_ASSERT(depth > 0, "call stack depth > 0");
    return true;
}
#else
static bool test_cm_backtrace(void)
{
    sys_printk("[%s] SKIP (CmbBacktrace not enabled)\r\n", __func__);
    return true;
}
#endif

/* ============================================================
 * 22. Workqueue（条件：WORKQUEUE）
 * ============================================================ */
#ifdef WORKQUEUE
static bool test_workqueue(void)
{
    static struct work_struct wq_imm;
    static struct delayed_work wq_del;
    static volatile bool imm_done = false;
    static volatile bool del_done = false;

    sys_printk("[%s]\r\n", __func__);
    imm_done = false; del_done = false;

    void imm_handler(struct work_struct *ws) {
        (void)ws;
        imm_done = true;
        sys_printk("  immediate work executed at tick=%lu\r\n",
                   (unsigned long)rtos_get_tick_count());
    }

    void del_handler(struct work_struct *ws) {
        (void)ws;
        del_done = true;
        sys_printk("  delayed work executed at tick=%lu\r\n",
                   (unsigned long)rtos_get_tick_count());
    }

    INIT_WORK(&wq_imm, imm_handler);
    INIT_DELAYED_WORK(&wq_del, del_handler);

    schedule_work(&wq_imm);
    schedule_delayed_work(&wq_del, 300);

    /* 等 System Workqueue 处理 */
    uint32_t start = rtos_get_tick_count();
    while (!imm_done || !del_done) {
        if ((int32_t)(rtos_get_tick_count() - start) > 2000) break;
        rtos_task_delay(20);
    }

    TEST_ASSERT(imm_done, "immediate work ran");
    TEST_ASSERT(del_done, "delayed work ran");
    return true;
}
#else
static bool test_workqueue(void)
{
    sys_printk("[%s] SKIP (Workqueue not enabled)\r\n", __func__);
    return true;
}
#endif

/* ============================================================
 * 测试运行器
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;
    static const struct {
        const char *name;
        bool (*fn)(void);
    } s_tests[] = {
        {"state",              test_state},
        {"mutex_basic",        test_mutex_basic},
        {"mutex_recursive",    test_mutex_recursive},
        {"mutex_priority",     test_mutex_priority},
        {"semaphore_binary",   test_semaphore_binary},
        {"semaphore_counting", test_semaphore_counting},
        {"queue_basic",        test_queue_basic},
        {"queue_blocking",     test_queue_blocking},
        {"suspend_resume",     test_suspend_resume},
        {"self_suspend",       test_self_suspend},
        {"priority",           test_priority},
        {"selfdelete",         test_selfdelete},
        {"sched_lock",         test_sched_lock},
        {"yield",              test_yield},
        {"delay_until",        test_delay_until},
        {"stack_free",         test_stack_free},
        {"abort_delay",        test_abort_delay},
        {"queue_isr",          test_queue_isr},
        {"semaphore_isr",      test_semaphore_isr},
        {"fpu",                test_fpu},
        {"cm_backtrace",       test_cm_backtrace},
        {"workqueue",          test_workqueue},
    };

    int total = (int)(sizeof(s_tests) / sizeof(s_tests[0]));
    int pass = 0;

    for (int i = 0; i < total; i++) {
        sys_printk("[%2d/%2d] %-22s ", i + 1, total, s_tests[i].name);
        bool ok = s_tests[i].fn();
        if (ok) { pass++; sys_printk("PASS\r\n"); }
        else     {        sys_printk("FAIL\r\n"); }
        rtos_task_delay(50);
    }

    sys_printk("\r\n========================================\r\n");
    sys_printk("  Results: %d / %d PASS\r\n", pass, total);
    sys_printk("========================================\r\n");

    /* 测试套件结束，关闭 ISR 测试逻辑，SysTick_Handler 回到最简形态 */
    s_suite_done = true;

    rtos_task_delete(NULL);
}

#endif /* TEST_ALL */
