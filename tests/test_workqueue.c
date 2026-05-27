/*
 * Test: Workqueue scheduling
 *
 * 验证项：
 *  - INIT_WORK / schedule_work（立即执行）
 *  - INIT_DELAYED_WORK / schedule_delayed_work（延迟执行）
 *  - 工作队列的 FIFO 调度与延迟到期机制
 */

#include "linRTOS.h"
#include "cli_io.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_WORKQUEUE)

#include "workqueue.h"

/* ============================================================
 * 静态资源
 * ============================================================ */

static struct work_struct wq_immediate;
static struct delayed_work wq_delayed_300;
static struct delayed_work wq_delayed_600;
static int wq_inited = 0;

/* ============================================================
 * 工作回调
 * ============================================================ */

static void wq_immediate_handler(struct work_struct *work)
{
	(void)work;
	sys_printk("[WQ] immediate work executed at tick %u\r\n",
		   (unsigned)jiffies);
}

static void wq_delayed_300_handler(struct work_struct *work)
{
	(void)work;
	sys_printk("[WQ] delayed work (300 ticks) executed at tick %u\r\n",
		   (unsigned)jiffies);
}

static void wq_delayed_600_handler(struct work_struct *work)
{
	(void)work;
	sys_printk("[WQ] delayed work (600 ticks) executed at tick %u\r\n",
		   (unsigned)jiffies);
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
	(void)param;

	sys_printk("=== Test: Workqueue ===\r\n");

	if (!wq_inited) {
		INIT_WORK(&wq_immediate, wq_immediate_handler);
		INIT_DELAYED_WORK(&wq_delayed_300, wq_delayed_300_handler);
		INIT_DELAYED_WORK(&wq_delayed_600, wq_delayed_600_handler);
		wq_inited = 1;
	}

	sys_printk("[WQ] Scheduling jobs at tick %u...\r\n", (unsigned)jiffies);

	schedule_work(&wq_immediate);
	schedule_delayed_work(&wq_delayed_300, 300);
	schedule_delayed_work(&wq_delayed_600, 600);

	sys_printk("[WQ] Jobs scheduled, watch tick loop output\r\n");

	/* 当前任务结束，workqueue 回调由 LinCLI 调度器在后续 tick 中执行 */
	rtos_task_delete(NULL);
}

#endif /* TEST_WORKQUEUE */
