/*
 * Test: Queue blocking and timeout
 * 验证: 满队列 send 阻塞、消费者唤醒生产者、send/recv 超时精度
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_QUEUE_BLOCKING) && defined(ARCH_COMPILER_GCC)

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

static bool test_queue_blocking(void)
{
    static struct rtos_queue q;
    static uint8_t buf[3 * sizeof(uint32_t)];
    static volatile uint32_t pcnt, ccnt;
    static volatile int order_err = 0;

    pcnt = 0; ccnt = 0; order_err = 0;

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
            if (e == RTOS_OK) {
                ccnt++;
                if (v != i) order_err = 1;
            }
        }
        rtos_task_delete(NULL);
    }

    /* 1. blocking producer-consumer (capacity=3, 8 items) */
    rtos_queue_init(&q, buf, 3, sizeof(uint32_t));

    /* producer starts first at low prio, fills 3 then blocks */
    rtos_task_create(producer, "prod", s_stk0, 160, NULL, 2, NULL);
    rtos_task_delay(50);
    TEST_ASSERT(pcnt == 3, "producer should fill 3 then block");
    TEST_ASSERT(rtos_queue_is_full(&q), "queue should be full");

    /* consumer at high prio wakes producer */
    rtos_task_create(consumer, "cons", s_stk1, 160, NULL, 5, NULL);
    if (!wait_for_val(&pcnt, 8, 2000)) TEST_ASSERT(0, "timeout producer");
    if (!wait_for_val(&ccnt, 8, 2000)) TEST_ASSERT(0, "timeout consumer");
    TEST_ASSERT(pcnt == 8 && ccnt == 8, "all 8 items should be transferred");
    TEST_ASSERT(!order_err, "FIFO order should be correct");

    /* 2. send timeout test */
    rtos_queue_init(&q, buf, 3, sizeof(uint32_t));
    uint32_t d = 0;
    for (int i = 0; i < 3; i++) rtos_queue_send(&q, &d, RTOS_DONT_WAIT);
    uint32_t t0 = rtos_get_tick_count();
    rtos_err_t e = rtos_queue_send(&q, &d, 50);
    uint32_t t1 = rtos_get_tick_count();
    TEST_ASSERT(e == RTOS_ERR_TIMEOUT, "send timeout should return ERR_TIMEOUT");
    TEST_ASSERT((int32_t)(t1 - t0) >= 50, "send timeout should wait >= 50 ticks");

    /* 3. recv timeout test */
    for (int i = 0; i < 3; i++) rtos_queue_recv(&q, &d, RTOS_DONT_WAIT);
    t0 = rtos_get_tick_count();
    e = rtos_queue_recv(&q, &d, 50);
    t1 = rtos_get_tick_count();
    TEST_ASSERT(e == RTOS_ERR_TIMEOUT, "recv timeout should return ERR_TIMEOUT");
    TEST_ASSERT((int32_t)(t1 - t0) >= 50, "recv timeout should wait >= 50 ticks");

    return true;
}

TEST_CASE_REGISTER(queue_blocking, test_queue_blocking);

#endif
