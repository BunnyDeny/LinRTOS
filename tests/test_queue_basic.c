/*
 * Test: Queue basic API
 * 验证: init, send/recv FIFO, send_to_front, overwrite, peek, full/empty errors
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_QUEUE_BASIC)

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

static bool test_queue_basic(void)
{
    static struct rtos_queue q, q1;
    static uint8_t buf[5 * sizeof(uint32_t)];
    static uint8_t buf1[1 * sizeof(uint32_t)];
    static volatile uint32_t pcnt, ccnt;
    static volatile int fifo_err = 0;

    pcnt = 0; ccnt = 0; fifo_err = 0;

    void producer(void *p) {
        (void)p;
        for (uint32_t i = 0; i < 5; i++) {
            rtos_err_t e = rtos_queue_send(&q, &i, RTOS_WAIT_FOREVER);
            if (e == RTOS_OK) pcnt++;
        }
        rtos_task_delete(NULL);
    }

    void consumer(void *p) {
        (void)p;
        for (uint32_t i = 0; i < 5; i++) {
            uint32_t v;
            rtos_err_t e = rtos_queue_recv(&q, &v, RTOS_WAIT_FOREVER);
            if (e == RTOS_OK) {
                ccnt++;
                if (v != i) fifo_err = 1;
            }
        }
        rtos_task_delete(NULL);
    }

    /* 1. init */
    rtos_err_t e = rtos_queue_init(&q, buf, 5, sizeof(uint32_t));
    TEST_ASSERT(e == RTOS_OK, "queue init");
    TEST_ASSERT(rtos_queue_is_empty(&q), "should be empty");
    TEST_ASSERT(!rtos_queue_is_full(&q), "should not be full");
    TEST_ASSERT(rtos_queue_spaces_available(&q) == 5, "spaces should be 5");
    TEST_ASSERT(rtos_queue_messages_waiting(&q) == 0, "messages should be 0");

    /* 2. FIFO producer-consumer */
    rtos_task_create(producer, "prod", s_stk0, 160, NULL, 2, NULL);
    rtos_task_create(consumer, "cons", s_stk1, 160, NULL, 5, NULL);
    if (!wait_for_val(&pcnt, 5, 2000)) TEST_ASSERT(0, "timeout producer");
    if (!wait_for_val(&ccnt, 5, 2000)) TEST_ASSERT(0, "timeout consumer");
    TEST_ASSERT(rtos_queue_is_empty(&q), "should be empty after drain");
    TEST_ASSERT(!fifo_err, "FIFO order should be correct");

    /* 3. send_to_front — verify reverse order */
    uint32_t a = 10, b = 20, v;
    rtos_queue_send(&q, &a, RTOS_DONT_WAIT);
    rtos_queue_send_to_front(&q, &b, RTOS_DONT_WAIT);
    rtos_queue_recv(&q, &v, RTOS_DONT_WAIT);
    TEST_ASSERT(v == 20, "front: first should be 20");
    rtos_queue_recv(&q, &v, RTOS_DONT_WAIT);
    TEST_ASSERT(v == 10, "front: second should be 10");

    /* 4. overwrite */
    rtos_queue_init(&q1, buf1, 1, sizeof(uint32_t));
    uint32_t x = 100;
    rtos_queue_send(&q1, &x, RTOS_DONT_WAIT);
    x = 200;
    rtos_queue_overwrite(&q1, &x);
    rtos_queue_recv(&q1, &v, RTOS_DONT_WAIT);
    TEST_ASSERT(v == 200, "overwrite should replace value");

    /* 5. peek — read without removing */
    x = 300;
    rtos_queue_send(&q, &x, RTOS_DONT_WAIT);
    rtos_queue_peek(&q, &v, RTOS_DONT_WAIT);
    TEST_ASSERT(v == 300, "peek value should be 300");
    TEST_ASSERT(rtos_queue_messages_waiting(&q) == 1, "peek should not remove");
    rtos_queue_recv(&q, &v, RTOS_DONT_WAIT);
    TEST_ASSERT(v == 300, "real recv value should be 300");

    /* 6. full-send / empty-recv → ERR_RESOURCE */
    for (uint32_t i = 0; i < 5; i++) rtos_queue_send(&q, &i, RTOS_DONT_WAIT);
    TEST_ASSERT(rtos_queue_is_full(&q), "should be full");
    e = rtos_queue_send(&q, &x, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_ERR_RESOURCE, "send to full queue must return ERR_RESOURCE");
    for (uint32_t i = 0; i < 5; i++) { rtos_queue_recv(&q, &v, RTOS_DONT_WAIT); }
    e = rtos_queue_recv(&q, &v, RTOS_DONT_WAIT);
    TEST_ASSERT(e == RTOS_ERR_RESOURCE, "recv from empty must return ERR_RESOURCE");

    rtos_queue_delete(&q);
    return true;
}

TEST_CASE_REGISTER(queue_basic, test_queue_basic);

#endif
