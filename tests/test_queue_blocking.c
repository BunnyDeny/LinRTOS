#include "linRTOS.h"
#include "cli_io.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_QUEUE_BLOCKING)

static struct rtos_queue s_q;
static uint8_t s_buf[3 * sizeof(uint32_t)];
static volatile uint32_t s_pcnt = 0;
static volatile uint32_t s_ccnt = 0;

static uint32_t s_pstk[128];
static uint32_t s_cstk[128];

static void producer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 8; i++) {
        rtos_err_t e = rtos_queue_send(&s_q, &i, RTOS_WAIT_FOREVER);
        if (e == RTOS_OK) { s_pcnt++; sys_printk("[BLOCK] sent %lu\r\n", (unsigned long)i); }
    }
    rtos_task_delete(NULL);
}

static void consumer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t v;
        rtos_err_t e = rtos_queue_recv(&s_q, &v, RTOS_WAIT_FOREVER);
        if (e == RTOS_OK) { s_ccnt++; RTOS_ASSERT(v == i); sys_printk("[BLOCK] recv %lu\r\n", (unsigned long)v); }
    }
    rtos_task_delete(NULL);
}

void app_entry_task(void *param)
{
    (void)param;
    sys_printk("=== Test: Queue Blocking ===\r\n");
    rtos_queue_init(&s_q, s_buf, 3, sizeof(uint32_t));

    /* producer first (low prio), fills queue then blocks */
    rtos_task_create(producer, "prod", s_pstk, 128, NULL, 2, NULL);
    rtos_task_delay(50);
    RTOS_ASSERT(s_pcnt == 3);
    sys_printk("[BLOCK] producer blocked after 3\r\n");

    /* consumer (high prio) wakes producer */
    rtos_task_create(consumer, "cons", s_cstk, 128, NULL, 5, NULL);
    while (s_pcnt < 8 || s_ccnt < 8) rtos_task_delay(50);
    RTOS_ASSERT(s_pcnt == 8 && s_ccnt == 8);
    sys_printk("[BLOCK] prod/cons done\r\n");

    /* timeout test */
    rtos_queue_init(&s_q, s_buf, 3, sizeof(uint32_t));
    uint32_t d = 0;
    for (uint32_t i = 0; i < 3; i++) rtos_queue_send(&s_q, &d, RTOS_DONT_WAIT);
    uint32_t t0 = rtos_get_tick_count();
    rtos_err_t e = rtos_queue_send(&s_q, &d, 50);
    uint32_t t1 = rtos_get_tick_count();
    RTOS_ASSERT(e == RTOS_ERR_TIMEOUT);
    RTOS_ASSERT((int32_t)(t1 - t0) >= 50);
    sys_printk("[BLOCK] send-timeout OK (%lu ticks)\r\n", (unsigned long)(t1 - t0));

    for (uint32_t i = 0; i < 3; i++) rtos_queue_recv(&s_q, &d, RTOS_DONT_WAIT);
    t0 = rtos_get_tick_count();
    e = rtos_queue_recv(&s_q, &d, 50);
    t1 = rtos_get_tick_count();
    RTOS_ASSERT(e == RTOS_ERR_TIMEOUT);
    RTOS_ASSERT((int32_t)(t1 - t0) >= 50);
    sys_printk("[BLOCK] recv-timeout OK (%lu ticks)\r\n", (unsigned long)(t1 - t0));

    sys_printk("=== Test: Queue Blocking DONE ===\r\n");
    rtos_task_delete(NULL);
}

#endif
