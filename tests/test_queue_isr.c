#include "linRTOS.h"
#include "cli_io.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_QUEUE_ISR)

static struct rtos_queue s_q;
static uint8_t s_buf[5 * sizeof(uint32_t)];
static volatile bool s_done = false;
static volatile uint32_t s_sent = 0;
static volatile bool s_woken = false;
static uint32_t s_cstk[128];

/* 覆盖 SysTick_Handler，在 tick ISR 中 send_from_isr */
void SysTick_Handler(void)
{
    rtos_tick_handler();
    static uint32_t cnt = 0;
    if ((++cnt % 200 == 0) && !s_done) {
        uint32_t d = cnt;
        bool hp = false;
        if (rtos_queue_generic_send_from_isr(&s_q, &d, RTOS_QUEUE_SEND_BACK, &hp) == RTOS_OK) {
            s_sent++;
            if (hp) s_woken = true;
        }
    }
}

static void consumer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t v;
        if (rtos_queue_recv(&s_q, &v, RTOS_WAIT_FOREVER) == RTOS_OK) {
            sys_printk("[ISR] recv #%lu data=%lu\r\n", (unsigned long)(i+1), (unsigned long)v);
        }
    }
    s_done = true;
    rtos_task_delete(NULL);
}

void app_entry_task(void *param)
{
    (void)param;
    sys_printk("=== Test: Queue ISR ===\r\n");
    rtos_queue_init(&s_q, s_buf, 5, sizeof(uint32_t));
    rtos_task_create(consumer, "cons", s_cstk, 128, NULL, 5, NULL);
    while (!s_done) {
        rtos_task_delay(500);
        sys_printk("[ISR] sent=%lu woken=%d\r\n", (unsigned long)s_sent, s_woken ? 1 : 0);
    }
    sys_printk("=== Test: Queue ISR DONE ===\r\n");
    rtos_task_delete(NULL);
}

#endif
