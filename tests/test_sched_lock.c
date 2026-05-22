/*
 * Test: Scheduler lock
 *
 * 验证项：
 *  - rtos_sched_lock / rtos_sched_unlock 禁止/恢复抢占
 *  - 上锁期间高优先级任务就绪不会抢占
 *  - 解锁后高优先级任务立即抢占
 *  - 嵌套锁：多次 lock 后需要对应次数 unlock 才生效
 */

#include "linRTOS.h"

#ifdef TEST_SCHED_LOCK

extern void debug_printf(const char *fmt, ...);

/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_ctrl_stack[128];
static uint32_t task_high_stack[128];

static volatile uint32_t s_high_ran_tick = 0;

/* ============================================================
 * 高优先级任务 —— 只运行一次，记录被调度到的 tick
 * ============================================================ */

static void task_high(void *param)
{
    (void)param;
    s_high_ran_tick = rtos_get_tick_count();
    debug_printf("[HIGH] ran at tick=%lu\r\n",
                     (unsigned long)s_high_ran_tick);
    rtos_task_delete(NULL);
}

/* ============================================================
 * 控制任务 —— 测试单级锁和嵌套锁
 * ============================================================ */

static void task_ctrl(void *param)
{
    (void)param;
    uint32_t t;

    /* ---------- 测试 1：单级锁 ---------- */
    debug_printf("[CTRL] === test single lock ===\r\n");
    s_high_ran_tick = 0;

    rtos_sched_lock();
    t = rtos_get_tick_count();
    debug_printf("[CTRL] locked at tick=%lu\r\n", (unsigned long)t);

    rtos_task_create(task_high, "high", task_high_stack, 128, NULL, 3, NULL);

    if (s_high_ran_tick == 0) {
    debug_printf("[CTRL] inside lock: high NOT ran (ok)\r\n");
    } else {
    debug_printf("[CTRL] inside lock: high ALREADY ran at %lu (BUG!)\r\n",
                         (unsigned long)s_high_ran_tick);
    }

    rtos_sched_unlock();
    t = rtos_get_tick_count();
    debug_printf("[CTRL] unlocked at tick=%lu high_ran_at=%lu\r\n",
                     (unsigned long)t,
                     (unsigned long)s_high_ran_tick);

    /* 等待空闲任务回收 task_high 的 TCB */
    rtos_task_delay(2);

    /* ---------- 测试 2：嵌套锁 ---------- */
    debug_printf("[CTRL] === test nested lock ===\r\n");
    s_high_ran_tick = 0;

    rtos_sched_lock();
    rtos_sched_lock();  /* 嵌套 2 层 */
    rtos_sched_lock();  /* 嵌套 3 层 */

    t = rtos_get_tick_count();
    debug_printf("[CTRL] nested 3 locks at tick=%lu\r\n", (unsigned long)t);

    rtos_task_create(task_high, "high2", task_high_stack, 128, NULL, 3, NULL);

    rtos_sched_unlock();  /* 3 -> 2 */
    if (s_high_ran_tick == 0) {
    debug_printf("[CTRL] after 1st unlock: high NOT ran (ok, lock=2)\r\n");
    } else {
    debug_printf("[CTRL] after 1st unlock: high ALREADY ran (BUG!)\r\n");
    }

    rtos_sched_unlock();  /* 2 -> 1 */
    if (s_high_ran_tick == 0) {
    debug_printf("[CTRL] after 2nd unlock: high NOT ran (ok, lock=1)\r\n");
    } else {
    debug_printf("[CTRL] after 2nd unlock: high ALREADY ran (BUG!)\r\n");
    }

    rtos_sched_unlock();  /* 1 -> 0，真正释放 */
    t = rtos_get_tick_count();
    debug_printf("[CTRL] full unlocked at tick=%lu high_ran_at=%lu\r\n",
                     (unsigned long)t,
                     (unsigned long)s_high_ran_tick);

    for (;;) {
    debug_printf("[CTRL] idle heartbeat tick=%lu\r\n",
                         (unsigned long)rtos_get_tick_count());
        rtos_task_delay(1000);
    }
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    debug_printf("=== Test: Scheduler Lock ===\r\n");

    rtos_task_create(task_ctrl, "ctrl", task_ctrl_stack, 128, NULL, 1, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_SCHED_LOCK */
