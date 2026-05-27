#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_semaphore.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_SEMAPHORE_BINARY)

static struct rtos_queue s_sem;
static uint32_t s_pstk[128];
static uint32_t s_cstk[128];
static volatile uint32_t s_pcnt = 0;
static volatile uint32_t s_ccnt = 0;

static void producer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 5; i++) {
        rtos_task_delay(30);  /* 模拟生产耗时 */
        rtos_semaphore_give(&s_sem);
        s_pcnt++;
        sys_printk("[SEM-BIN] produced #%lu\r\n", (unsigned long)(i + 1));
    }
    rtos_task_delete(NULL);
}

static void consumer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 5; i++) {
        rtos_semaphore_take(&s_sem, RTOS_WAIT_FOREVER);
        s_ccnt++;
        sys_printk("[SEM-BIN] consumed #%lu\r\n", (unsigned long)(i + 1));
    }
    rtos_task_delete(NULL);
}

void app_entry_task(void *param)
{
    (void)param;
    sys_printk("=== Test: Semaphore Binary ===\r\n");

    rtos_semaphore_init_binary(&s_sem);

    /* 消费者先创建，阻塞等待信号量 */
    rtos_task_create(consumer, "cons", s_cstk, 128, NULL, 5, NULL);
    /* 生产者后创建，优先级较低 */
    rtos_task_create(producer, "prod", s_pstk, 128, NULL, 2, NULL);

    while (s_pcnt < 5 || s_ccnt < 5) {
        rtos_task_delay(50);
    }

    RTOS_ASSERT(s_pcnt == 5);
    RTOS_ASSERT(s_ccnt == 5);
    RTOS_ASSERT(rtos_semaphore_get_count(&s_sem) == 0);

    sys_printk("=== Test: Semaphore Binary DONE ===\r\n");
    rtos_task_delete(NULL);
}

#endif
