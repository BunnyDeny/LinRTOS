#include "linRTOS.h"
#include "cli_io.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_QUEUE_BASIC)

static struct rtos_queue s_q;
static uint8_t s_buf[5 * sizeof(uint32_t)];
static uint32_t s_pstk[128];
static uint32_t s_cstk[128];
static volatile uint32_t s_pcnt = 0;
static volatile uint32_t s_ccnt = 0;

static void producer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 5; i++) {
        rtos_err_t e = rtos_queue_send(&s_q, &i, RTOS_WAIT_FOREVER);
        RTOS_ASSERT(e == RTOS_OK);
        s_pcnt++;
        sys_printk("[BASIC] sent %lu\r\n", (unsigned long)i);
    }
    rtos_task_delete(NULL);
}

static void consumer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 5; i++) {
        uint32_t v;
        rtos_err_t e = rtos_queue_recv(&s_q, &v, RTOS_WAIT_FOREVER);
        RTOS_ASSERT(e == RTOS_OK);
        RTOS_ASSERT(v == i);
        s_ccnt++;
        sys_printk("[BASIC] recv %lu\r\n", (unsigned long)v);
    }
    rtos_task_delete(NULL);
}

void app_entry_task(void *param)
{
    (void)param;
    sys_printk("=== Test: Queue Basic ===\r\n");

    /* 1. init */
    rtos_err_t err = rtos_queue_init(&s_q, s_buf, 5, sizeof(uint32_t));
    RTOS_ASSERT(err == RTOS_OK);
    RTOS_ASSERT(rtos_queue_is_empty(&s_q));
    RTOS_ASSERT(rtos_queue_spaces_available(&s_q) == 5);
    sys_printk("[BASIC] init OK\r\n");

    /* 2. 多任务 FIFO 生产者-消费者 */
    rtos_task_create(producer, "prod", s_pstk, 128, NULL, 2, NULL);
    rtos_task_create(consumer, "cons", s_cstk, 128, NULL, 5, NULL);

    while (s_pcnt < 5 || s_ccnt < 5) rtos_task_delay(50);
    RTOS_ASSERT(rtos_queue_is_empty(&s_q));
    sys_printk("[BASIC] FIFO producer-consumer OK\r\n");

    /* 3. send_to_front */
    uint32_t a = 10, b = 20;
    rtos_queue_send(&s_q, &a, RTOS_DONT_WAIT);
    rtos_queue_send_to_front(&s_q, &b, RTOS_DONT_WAIT);
    uint32_t v;
    rtos_queue_recv(&s_q, &v, RTOS_DONT_WAIT); RTOS_ASSERT(v == 20);
    rtos_queue_recv(&s_q, &v, RTOS_DONT_WAIT); RTOS_ASSERT(v == 10);
    sys_printk("[BASIC] front OK\r\n");

    /* 4. overwrite (length must be 1) */
    static struct rtos_queue s_q1;
    static uint8_t s_buf1[1 * sizeof(uint32_t)];
    rtos_queue_init(&s_q1, s_buf1, 1, sizeof(uint32_t));
    uint32_t x = 100;
    rtos_queue_send(&s_q1, &x, RTOS_DONT_WAIT);
    x = 200;
    rtos_queue_overwrite(&s_q1, &x);
    rtos_queue_recv(&s_q1, &v, RTOS_DONT_WAIT);
    RTOS_ASSERT(v == 200);
    sys_printk("[BASIC] overwrite OK\r\n");

    /* 5. peek */
    x = 300;
    rtos_queue_send(&s_q, &x, RTOS_DONT_WAIT);
    rtos_queue_peek(&s_q, &v, RTOS_DONT_WAIT);
    RTOS_ASSERT(v == 300);
    RTOS_ASSERT(rtos_queue_messages_waiting(&s_q) == 1);
    rtos_queue_recv(&s_q, &v, RTOS_DONT_WAIT);
    RTOS_ASSERT(v == 300);
    sys_printk("[BASIC] peek OK\r\n");

    /* 6. non-blocking err */
    for (uint32_t i = 0; i < 5; i++) rtos_queue_send(&s_q, &i, RTOS_DONT_WAIT);
    err = rtos_queue_send(&s_q, &x, RTOS_DONT_WAIT);
    RTOS_ASSERT(err == RTOS_ERR_RESOURCE);
    for (uint32_t i = 0; i < 5; i++) rtos_queue_recv(&s_q, &v, RTOS_DONT_WAIT);
    err = rtos_queue_recv(&s_q, &v, RTOS_DONT_WAIT);
    RTOS_ASSERT(err == RTOS_ERR_RESOURCE);
    sys_printk("[BASIC] err-on-full-empty OK\r\n");

    rtos_queue_delete(&s_q);
    sys_printk("=== Test: Queue Basic DONE ===\r\n");
    rtos_task_delete(NULL);
}

#endif
