/*
 * Test: Workqueue scheduling (conditional: WORKQUEUE)
 * 验证: schedule_work 立即执行, schedule_delayed_work 延迟到期执行
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_WORKQUEUE)

#ifdef WORKQUEUE
#include "workqueue.h"
#endif

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

#ifdef WORKQUEUE
static bool test_workqueue(void)
{
    static struct work_struct wq_imm;
    static struct delayed_work wq_del;
    static volatile bool imm_done = false;
    static volatile bool del_done = false;

    sys_printk("[%s]\r\n", __func__);
    imm_done = false; del_done = false;

    void imm_handler(struct work_struct *ws) {
        (void)ws;
        imm_done = true;
        sys_printk("  immediate work executed at tick=%lu\r\n",
                   (unsigned long)rtos_get_tick_count());
    }

    void del_handler(struct work_struct *ws) {
        (void)ws;
        del_done = true;
        sys_printk("  delayed work executed at tick=%lu\r\n",
                   (unsigned long)rtos_get_tick_count());
    }

    INIT_WORK(&wq_imm, imm_handler);
    INIT_DELAYED_WORK(&wq_del, del_handler);

    schedule_work(&wq_imm);
    schedule_delayed_work(&wq_del, 300);

    /* Wait for System Workqueue to process both */
    uint32_t start = rtos_get_tick_count();
    uint32_t del_complete_tick = 0;
    while (!imm_done || !del_done) {
        if ((int32_t)(rtos_get_tick_count() - start) > 2000) break;
        rtos_task_delay(20);
        if (del_done && del_complete_tick == 0) {
            del_complete_tick = rtos_get_tick_count();
        }
    }

    TEST_ASSERT(imm_done, "immediate work should have run");
    TEST_ASSERT(del_done, "delayed work should have run");

    return true;
}
#else
static bool test_workqueue(void)
{
    sys_printk("[%s] SKIP (Workqueue not enabled)\r\n", __func__);
    return true;
}
#endif

TEST_CASE_REGISTER(workqueue, test_workqueue);

#endif
