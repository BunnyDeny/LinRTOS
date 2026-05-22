/*
 * Test: FPU context switch + self-delete recycle
 *
 * 验证项：
 *  - 多任务抢占调度
 *  - 浮点上下文切换（若开启 FPU）
 *  - 任务自删除 + 空闲任务 TCB 回收重用
 */

#include "linRTOS.h"

#ifdef TEST_FPU_SELFDELETE

#include <math.h>

extern void debug_printf(const char *fmt, ...);

/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_high_stack[256];
static uint32_t task_low_stack[256];
static uint32_t task_fp_a_stack[512];
static uint32_t task_fp_b_stack[512];
static uint32_t task_test_stack[128];

static volatile uint32_t s_high_count = 0;
static volatile uint32_t s_low_count = 0;
static volatile uint32_t s_test_count = 0;

/* ============================================================
 * 子任务
 * ============================================================ */

static void task_high(void *param)
{
    (void)param;
    for (;;) {
        s_high_count++;
        debug_printf("[HIGH] tick=%lu count=%lu\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (unsigned long)s_high_count);
        rtos_task_delay(500);
    }
}

/* 自删任务 —— 运行一次后自删，验证空闲任务回收 TCB */
static void task_test(void *param)
{
    (void)param;
    s_test_count++;
    debug_printf("[TEST] run #%lu, tick=%lu\r\n",
                 (unsigned long)s_test_count,
                 (unsigned long)rtos_get_tick_count());
    rtos_task_delay(300);
    debug_printf("[TEST] self-delete\r\n");
    rtos_task_delete(NULL);
}

static void task_low(void *param)
{
    (void)param;
    for (;;) {
        s_low_count++;
        debug_printf("[LOW ] tick=%lu count=%lu test_count=%lu\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (unsigned long)s_low_count,
                     (unsigned long)s_test_count);
        /* 周期性创建自删任务，验证 TCB 回收重用 */
        rtos_err_t err = rtos_task_create(task_test, "test",
                                          task_test_stack, 128, NULL, 2, NULL);
        if (err != RTOS_OK) {
            debug_printf("[LOW ] create test FAILED! err=%d (pool exhausted?)\r\n",
                         (int)err);
        } else {
            debug_printf("[LOW ] create test OK\r\n");
        }
        rtos_task_delay(1000);
    }
}

/* 浮点任务 A —— 三角函数累加验证 */
static void task_fp_a(void *param)
{
    (void)param;
    float sum = 0.0f;
    for (;;) {
        sum += 0.1f;
        float s = sinf(sum);
        float c = cosf(sum);
        float chk = sqrtf(fabsf(s) + fabsf(c));
        debug_printf("[FPA ] tick=%lu sum=%.6f sin=%.6f cos=%.6f chk=%.6f\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (double)sum, (double)s, (double)c, (double)chk);
        rtos_task_delay(300);
    }
}

/* 浮点任务 B —— 乘幂/对数验证 */
static void task_fp_b(void *param)
{
    (void)param;
    float val = 1.0f;
    for (;;) {
        val = val * 1.618f + 0.314f;
        if (val > 500.0f) {
            val = 1.0f;
        }
        float t = tanf(val);
        float l = logf(val);
        debug_printf("[FPB ] tick=%lu val=%.6f tan=%.6f log=%.6f\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (double)val, (double)t, (double)l);
        rtos_task_delay(700);
    }
}

/* ============================================================
 * 统一入口
 * ============================================================
 *
 * main.c 中通过 app_entry_start() 启动本测试。
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    debug_printf("=== Test: FPU + Self-Delete ===\r\n");

    rtos_task_create(task_fp_a, "fp_a", task_fp_a_stack, 256, NULL, 3, NULL);
    rtos_task_create(task_high, "high", task_high_stack, 128, NULL, 2, NULL);
    rtos_task_create(task_fp_b, "fp_b", task_fp_b_stack, 256, NULL, 2, NULL);
    rtos_task_create(task_low,  "low",  task_low_stack,  128, NULL, 1, NULL);

    /* 入口任务完成使命后自删，不再占用 TCB */
    rtos_task_delete(NULL);
}

#endif /* TEST_FPU_SELFDELETE */
