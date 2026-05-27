#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_semaphore.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_SEMAPHORE_COUNTING)

static struct rtos_queue s_sem;
static uint32_t s_p1stk[128];
static uint32_t s_p2stk[128];
static uint32_t s_cstk[128];
static volatile uint32_t s_pcnt = 0;
static volatile uint32_t s_ccnt = 0;

static void producer1(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 3; i++) {
        rtos_task_delay(20);
        rtos_semaphore_give(&s_sem);
        s_pcnt++;
        sys_printk("[SEM-CNT] P1 produced\r\n");
    }
    rtos_task_delete(NULL);
}

static void producer2(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 3; i++) {
        rtos_task_delay(25);
        rtos_semaphore_give(&s_sem);
        s_pcnt++;
        sys_printk("[SEM-CNT] P2 produced\r\n");
    }
    rtos_task_delete(NULL);
}

static void consumer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 6; i++) {
        rtos_semaphore_take(&s_sem, RTOS_WAIT_FOREVER);
        s_ccnt++;
        sys_printk("[SEM-CNT] consumed #%lu\r\n", (unsigned long)(i + 1));
    }
    rtos_task_delete(NULL);
}

void app_entry_task(void *param)
{
    (void)param;
    sys_printk("=== Test: Semaphore Counting ===\r\n");

    /* max=5, initial=0 */
    rtos_semaphore_init_counting(&s_sem, 5, 0);

    rtos_task_create(consumer, "cons", s_cstk, 128, NULL, 5, NULL);
    rtos_task_create(producer1, "p1", s_p1stk, 128, NULL, 2, NULL);
    rtos_task_create(producer2, "p2", s_p2stk, 128, NULL, 3, NULL);

    while (s_pcnt < 6 || s_ccnt < 6) {
        rtos_task_delay(50);
    }

    RTOS_ASSERT(s_pcnt == 6);
    RTOS_ASSERT(s_ccnt == 6);
    sys_printk("=== Test: Semaphore Counting DONE ===\r\n");
    rtos_task_delete(NULL);
}

#endif
