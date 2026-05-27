#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_mutex.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_MUTEX_BASIC)

static struct rtos_queue s_mtx;
static volatile uint32_t s_shared = 0;
static uint32_t s_a_stk[128];
static uint32_t s_b_stk[128];
static volatile uint32_t s_a_done = 0;
static volatile uint32_t s_b_done = 0;

static void task_a(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 5; i++) {
        rtos_mutex_take(&s_mtx, RTOS_WAIT_FOREVER);
        uint32_t tmp = s_shared;
        rtos_task_delay(10);  /* 模拟临界区耗时 */
        s_shared = tmp + 1;
        rtos_mutex_give(&s_mtx);
        sys_printk("[MUTEX] task A inc shared=%lu\r\n", (unsigned long)s_shared);
    }
    s_a_done = 1;
    rtos_task_delete(NULL);
}

static void task_b(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 5; i++) {
        rtos_mutex_take(&s_mtx, RTOS_WAIT_FOREVER);
        uint32_t tmp = s_shared;
        rtos_task_delay(15);  /* 模拟临界区耗时 */
        s_shared = tmp + 1;
        rtos_mutex_give(&s_mtx);
        sys_printk("[MUTEX] task B inc shared=%lu\r\n", (unsigned long)s_shared);
    }
    s_b_done = 1;
    rtos_task_delete(NULL);
}

static volatile rtos_err_t s_bad_err = RTOS_OK;
static uint32_t s_bad_stk[128];

static void bad_giver(void *p)
{
    (void)p;
    s_bad_err = rtos_mutex_give(&s_mtx);
    rtos_task_delete(NULL);
}

void app_entry_task(void *param)
{
    (void)param;
    sys_printk("=== Test: Mutex Basic ===\r\n");

    rtos_mutex_init(&s_mtx);
    s_shared = 0;

    /* 两个任务并发修改共享变量，验证互斥保护 */
    rtos_task_create(task_a, "A", s_a_stk, 128, NULL, 3, NULL);
    rtos_task_create(task_b, "B", s_b_stk, 128, NULL, 4, NULL);

    while (!s_a_done || !s_b_done) {
        rtos_task_delay(50);
    }

    RTOS_ASSERT(s_shared == 10);
    sys_printk("[MUTEX] final shared=%lu (expect 10)\r\n", (unsigned long)s_shared);

    /* 验证非持有者 give 被拒绝 */
    rtos_err_t e = rtos_mutex_take(&s_mtx, RTOS_DONT_WAIT);
    RTOS_ASSERT(e == RTOS_OK);
    RTOS_ASSERT(rtos_mutex_get_holder(&s_mtx) == rtos_task_get_current());

    rtos_task_create(bad_giver, "bad", s_bad_stk, 128, NULL, 5, NULL);
    rtos_task_delay(50);
    RTOS_ASSERT(s_bad_err == RTOS_ERR_STATE);
    sys_printk("[MUTEX] bad giver rejected OK\r\n");

    rtos_mutex_give(&s_mtx);
    rtos_mutex_delete(&s_mtx);
    sys_printk("=== Test: Mutex Basic DONE ===\r\n");
    rtos_task_delete(NULL);
}

#endif
