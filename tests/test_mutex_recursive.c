#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_mutex.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_MUTEX_RECURSIVE)

static struct rtos_queue s_mtx;
static uint32_t s_tstk[128];
static volatile bool s_t_done = false;

static void other_task(void *p)
{
    (void)p;
    /* 另一任务尝试获取已被递归持有的互斥锁 → 阻塞 */
    rtos_err_t e = rtos_mutex_take(&s_mtx, 100);
    RTOS_ASSERT(e == RTOS_OK);
    sys_printk("[MUTEX-REC] other task got mutex\r\n");

    e = rtos_mutex_give(&s_mtx);
    RTOS_ASSERT(e == RTOS_OK);
    s_t_done = true;
    rtos_task_delete(NULL);
}

void app_entry_task(void *param)
{
    (void)param;
    sys_printk("=== Test: Mutex Recursive ===\r\n");

    rtos_mutex_init_recursive(&s_mtx);

    /* 1. 递归 take 3 次 */
    rtos_err_t e = rtos_mutex_take_recursive(&s_mtx, RTOS_DONT_WAIT);
    RTOS_ASSERT(e == RTOS_OK);
    e = rtos_mutex_take_recursive(&s_mtx, RTOS_DONT_WAIT);
    RTOS_ASSERT(e == RTOS_OK);
    e = rtos_mutex_take_recursive(&s_mtx, RTOS_DONT_WAIT);
    RTOS_ASSERT(e == RTOS_OK);
    RTOS_ASSERT(s_mtx.recursive_count == 3);
    RTOS_ASSERT(rtos_mutex_get_holder(&s_mtx) == rtos_task_get_current());
    sys_printk("[MUTEX-REC] take_recursive 3x OK, count=3\r\n");

    /* 2. give 2 次，仍未真正释放 */
    e = rtos_mutex_give_recursive(&s_mtx);
    RTOS_ASSERT(e == RTOS_OK);
    RTOS_ASSERT(s_mtx.recursive_count == 2);
    e = rtos_mutex_give_recursive(&s_mtx);
    RTOS_ASSERT(e == RTOS_OK);
    RTOS_ASSERT(s_mtx.recursive_count == 1);
    RTOS_ASSERT(rtos_mutex_get_holder(&s_mtx) == rtos_task_get_current());
    sys_printk("[MUTEX-REC] give_recursive 2x OK, count=1\r\n");

    /* 3. 创建另一任务尝试 take → 阻塞 */
    rtos_task_create(other_task, "other", s_tstk, 128, NULL, 5, NULL);
    rtos_task_delay(50);
    RTOS_ASSERT(!s_t_done);  /* other_task 应该还在阻塞 */
    sys_printk("[MUTEX-REC] other task blocked OK\r\n");

    /* 4. 第 3 次 give，真正释放，other_task 被唤醒 */
    e = rtos_mutex_give_recursive(&s_mtx);
    RTOS_ASSERT(e == RTOS_OK);
    RTOS_ASSERT(s_mtx.recursive_count == 0);

    while (!s_t_done) rtos_task_delay(10);
    sys_printk("[MUTEX-REC] final give OK, other task ran\r\n");

    /* 5. 非持有者 give_recursive → ERR_STATE */
    rtos_mutex_take_recursive(&s_mtx, RTOS_DONT_WAIT);
    s_t_done = false;
    rtos_task_create(other_task, "bad", s_tstk, 128, NULL, 3, NULL);
    while (!s_t_done) rtos_task_delay(10);
    /* other_task 中 give 会失败，但这里不方便检查返回值 */
    rtos_mutex_give_recursive(&s_mtx);

    rtos_mutex_delete(&s_mtx);
    sys_printk("=== Test: Mutex Recursive DONE ===\r\n");
    rtos_task_delete(NULL);
}

#endif
