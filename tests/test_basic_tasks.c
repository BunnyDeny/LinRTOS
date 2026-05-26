/*
 * Test: Basic multi-task + delay
 *
 * 验证项：
 *  - 多任务创建与删除
 *  - rtos_task_delay 相对延时
 *  - rtos_task_delay_until 绝对周期延时
 *  - 抢占式优先级调度（高优先级打断低优先级）
 *  - 同优先级时间片轮转（若开启）
 */

#include "linRTOS.h"
#include "cli_io.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_BASIC_TASKS)


/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_high_stack[128];
static uint32_t task_mid_stack[128];
static uint32_t task_low_stack[128];
static uint32_t task_periodic_stack[128];

static volatile uint32_t s_high_count = 0;
static volatile uint32_t s_mid_count  = 0;
static volatile uint32_t s_low_count  = 0;
static volatile uint32_t s_periodic_count = 0;

/* ============================================================
 * 高优先级任务 —— 频繁运行，验证抢占
 * ============================================================ */

static void task_high(void *param)
{
    (void)param;
    for (;;) {
        s_high_count++;
        sys_printk("[HIGH] tick=%lu count=%lu\r\n",
                         (unsigned long)rtos_get_tick_count(),
                         (unsigned long)s_high_count);
        rtos_task_delay(200);
    }
}

/* ============================================================
 * 中优先级任务 —— 中等频率
 * ============================================================ */

static void task_mid(void *param)
{
    (void)param;
    for (;;) {
        s_mid_count++;
        sys_printk("[MID ] tick=%lu count=%lu\r\n",
                         (unsigned long)rtos_get_tick_count(),
                         (unsigned long)s_mid_count);
        rtos_task_delay(400);
    }
}

/* ============================================================
 * 低优先级任务 —— 慢速运行，验证高优先级抢占时被打断
 * ============================================================ */

static void task_low(void *param)
{
    (void)param;
    for (;;) {
        s_low_count++;
        sys_printk("[LOW ] tick=%lu count=%lu\r\n",
                         (unsigned long)rtos_get_tick_count(),
                         (unsigned long)s_low_count);
        rtos_task_delay(800);
    }
}

/* ============================================================
 * 周期任务 —— 验证 rtos_task_delay_until 绝对周期精度
 * ============================================================ */

static void task_periodic(void *param)
{
    (void)param;
    uint32_t prev_wake = rtos_get_tick_count();
    for (;;) {
        s_periodic_count++;
        uint32_t now = rtos_get_tick_count();
        int32_t jitter = (int32_t)(now - prev_wake);
        sys_printk("[PER ] tick=%lu count=%lu jitter=%ld\r\n",
                         (unsigned long)now,
                         (unsigned long)s_periodic_count,
                         (long)jitter);
        rtos_task_delay_until(&prev_wake, 500);
    }
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    sys_printk("=== Test: Basic Tasks ===\r\n");

    rtos_task_create(task_high,     "high",     task_high_stack,     128, NULL, 3, NULL);
    rtos_task_create(task_mid,      "mid",      task_mid_stack,      128, NULL, 2, NULL);
    rtos_task_create(task_low,      "low",      task_low_stack,      128, NULL, 1, NULL);
    rtos_task_create(task_periodic, "periodic", task_periodic_stack, 128, NULL, 2, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_BASIC_TASKS */
