#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_semaphore.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_SEMAPHORE_ISR)

static struct rtos_queue s_sem;
static volatile uint32_t s_sent = 0;
static volatile bool s_done = false;
static volatile bool s_woken = false;
static uint32_t s_cstk[128];

/* 覆盖 SysTick_Handler，在 tick ISR 中 give_from_isr */
void SysTick_Handler(void)
{
    rtos_tick_handler();
    static uint32_t cnt = 0;
    if ((++cnt % 200 == 0) && !s_done) {
        bool hp = false;
        if (rtos_semaphore_give_from_isr(&s_sem, &hp) == RTOS_OK) {
            s_sent++;
            if (hp) s_woken = true;
        }
    }
}

static void consumer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 5; i++) {
        rtos_err_t e = rtos_semaphore_take(&s_sem, RTOS_WAIT_FOREVER);
        if (e == RTOS_OK) {
            sys_printk("[SEM-ISR] take #%lu OK\r\n", (unsigned long)(i + 1));
        }
    }
    s_done = true;
    rtos_task_delete(NULL);
}

void app_entry_task(void *param)
{
    (void)param;
    sys_printk("=== Test: Semaphore ISR ===\r\n");

    rtos_semaphore_init_binary(&s_sem);
    rtos_task_create(consumer, "cons", s_cstk, 128, NULL, 5, NULL);

    while (!s_done) {
        rtos_task_delay(500);
        sys_printk("[SEM-ISR] sent=%lu woken=%d\r\n",
                   (unsigned long)s_sent, s_woken ? 1 : 0);
    }

    RTOS_ASSERT(s_sent >= 5);
    sys_printk("=== Test: Semaphore ISR DONE ===\r\n");
    rtos_task_delete(NULL);
}

#endif
