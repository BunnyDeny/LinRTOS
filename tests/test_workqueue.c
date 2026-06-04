/*
 * Test: Workqueue scheduling (conditional: WORKQUEUE)
 *
 * 验证: schedule_work 近即时执行, schedule_delayed_work 延迟到期执行,
 *       ISR 中部分行打印的正确性.
 *
 * workqueue 由 scheduler 任务驱动(任务上下文), jiffies 由 SysTick 递增.
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_WORKQUEUE) && defined(ARCH_COMPILER_GCC)

#ifdef WORKQUEUE
#include "workqueue.h"
#endif

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { cli_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

#ifdef WORKQUEUE
static bool test_workqueue(void)
{
    static struct work_struct wq_imm;
    static struct delayed_work wq_del;
    static volatile bool imm_done = false;
    static volatile bool del_done = false;
    static volatile uint32_t imm_tick = 0;
    static volatile uint32_t del_tick = 0;

    imm_done = false; del_done = false;
    imm_tick = 0; del_tick = 0;

    void imm_handler(struct work_struct *ws) {
        (void)ws;
        imm_done = true;
        imm_tick = rtos_get_tick_count();
        /* workqueue runs in task context, each cli_printk is atomic */
        cli_printk("  [WQ] immediate\r\n");
    }

    void del_handler(struct work_struct *ws) {
        (void)ws;
        del_done = true;
        del_tick = rtos_get_tick_count();
    }

    INIT_WORK(&wq_imm, imm_handler);
    INIT_DELAYED_WORK(&wq_del, del_handler);

    uint32_t sched_tick = rtos_get_tick_count();

    schedule_work(&wq_imm);
    schedule_delayed_work(&wq_del, 300);

    /* Wait for both to complete */
    while (!imm_done || !del_done) {
        if ((int32_t)(rtos_get_tick_count() - sched_tick) > 2000) break;
        rtos_task_delay(20);
    }

    TEST_ASSERT(imm_done, "immediate work should have run");
    TEST_ASSERT(del_done, "delayed work should have run");

    {
        int32_t dt = (int32_t)(imm_tick - sched_tick);
        cli_printk("  immediate after %ld ticks\r\n", (long)dt);
        TEST_ASSERT(dt >= 0 && dt < 100,
                    "immediate work should fire < 100 ticks");
    }
    {
        int32_t dt = (int32_t)(del_tick - sched_tick);
        cli_printk("  delayed after %ld ticks (expected ~300)\r\n", (long)dt);
        TEST_ASSERT(dt >= 250 && dt <= 400,
                    "delayed work should fire in 250-400 ticks");
    }

    /* batch-mode split-line: two calls form one logical line */
    cli_printk_batch_begin();
    cli_printk("  [WQ] batch split");
    cli_printk(" line\r\n");
    cli_printk_batch_end();

    return true;
}
#else
static bool test_workqueue(void)
{
    cli_printk("  SKIP\n");
    return true;
}
#endif

TEST_CASE_REGISTER(workqueue, test_workqueue);

#endif
