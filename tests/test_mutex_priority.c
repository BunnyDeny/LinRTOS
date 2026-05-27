#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_mutex.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_MUTEX_PRIORITY)

static struct rtos_queue s_mtx;
static uint32_t s_lstk[128];
static uint32_t s_hstk[128];
static volatile uint32_t s_l_prio = 0;
static volatile uint32_t s_l_prio_after = 0;
static volatile bool s_high_done = false;

static void low_task(void *p)
{
    (void)p;
    rtos_err_t e = rtos_mutex_take(&s_mtx, RTOS_WAIT_FOREVER);
    RTOS_ASSERT(e == RTOS_OK);
    sys_printk("[MUTEX-PRIO] low task got mutex (orig prio=2)\r\n");

    /* delay 让 high_task 运行并阻塞在互斥锁上 */
    rtos_task_delay(100);

    /* 此时优先级应该已被提升到 5 */
    s_l_prio = rtos_task_get_priority(NULL);
    sys_printk("[MUTEX-PRIO] low task prio during hold = %lu\r\n",
               (unsigned long)s_l_prio);
    RTOS_ASSERT(s_l_prio == 5);

    /* 释放互斥锁 */
    e = rtos_mutex_give(&s_mtx);
    RTOS_ASSERT(e == RTOS_OK);

    /* 释放后恢复原始优先级 */
    s_l_prio_after = rtos_task_get_priority(NULL);
    sys_printk("[MUTEX-PRIO] low task prio after give = %lu\r\n",
               (unsigned long)s_l_prio_after);
    RTOS_ASSERT(s_l_prio_after == 2);

    rtos_task_delete(NULL);
}

static void high_task(void *p)
{
    (void)p;
    /* 稍等片刻确保 low_task 先拿到互斥锁 */
    rtos_task_delay(30);

    sys_printk("[MUTEX-PRIO] high task trying to take mutex...\r\n");
    rtos_err_t e = rtos_mutex_take(&s_mtx, RTOS_WAIT_FOREVER);
    RTOS_ASSERT(e == RTOS_OK);
    sys_printk("[MUTEX-PRIO] high task got mutex\r\n");

    e = rtos_mutex_give(&s_mtx);
    RTOS_ASSERT(e == RTOS_OK);
    s_high_done = true;
    rtos_task_delete(NULL);
}

void app_entry_task(void *param)
{
    (void)param;
    sys_printk("=== Test: Mutex Priority Inheritance ===\r\n");

    rtos_mutex_init(&s_mtx);

    /* low 先创建并运行，获取互斥锁 */
    rtos_task_create(low_task, "low", s_lstk, 128, NULL, 2, NULL);
    /* high 后创建，但优先级更高，会抢占 low */
    rtos_task_create(high_task, "high", s_hstk, 128, NULL, 5, NULL);

    while (!s_high_done) rtos_task_delay(50);

    RTOS_ASSERT(s_l_prio == 5);
    RTOS_ASSERT(s_l_prio_after == 2);

    rtos_mutex_delete(&s_mtx);
    sys_printk("=== Test: Mutex Priority Inheritance DONE ===\r\n");
    rtos_task_delete(NULL);
}

#endif
