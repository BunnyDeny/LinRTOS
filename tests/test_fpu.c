/*
 * Test: FPU context switch
 *
 * 验证项：
 *  - 多浮点任务抢占调度时 FPU 寄存器上下文正确保存/恢复
 *  - 高优先级干扰任务频繁抢占浮点任务
 */

#include "linRTOS.h"

#ifdef TEST_FPU

#include <math.h>

extern void debug_printf(const char *fmt, ...);

/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_fp_a_stack[512];
static uint32_t task_fp_b_stack[512];
static uint32_t task_intr_stack[128];

/* ============================================================
 * 浮点任务 A —— 三角函数累加验证
 * ============================================================ */

static void task_fp_a(void *param)
{
    (void)param;
    float sum = 0.0f;
    for (;;) {
        sum += 0.1f;
        float s = sinf(sum);
        float c = cosf(sum);
        float chk = sqrtf(fabsf(s) + fabsf(c));
        RTOS_ENTER_CRITICAL();
        debug_printf("[FPA ] tick=%lu sum=%.6f sin=%.6f cos=%.6f chk=%.6f\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (double)sum, (double)s, (double)c, (double)chk);
        RTOS_EXIT_CRITICAL();
        rtos_task_delay(300);
    }
}

/* ============================================================
 * 浮点任务 B —— 乘幂/对数验证
 * ============================================================ */

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
        RTOS_ENTER_CRITICAL();
        debug_printf("[FPB ] tick=%lu val=%.6f tan=%.6f log=%.6f\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (double)val, (double)t, (double)l);
        RTOS_EXIT_CRITICAL();
        rtos_task_delay(700);
    }
}

/* ============================================================
 * 干扰任务 —— 高优先级频繁抢占，验证抢占时 FPU 上下文保存
 * ============================================================ */

static void task_intr(void *param)
{
    (void)param;
    volatile uint32_t count = 0;
    for (;;) {
        count++;
        RTOS_ENTER_CRITICAL();
        debug_printf("[INTR] tick=%lu count=%lu\r\n",
                     (unsigned long)rtos_get_tick_count(),
                     (unsigned long)count);
        RTOS_EXIT_CRITICAL();
        rtos_task_delay(200);
    }
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    RTOS_ENTER_CRITICAL();
    debug_printf("=== Test: FPU Context Switch ===\r\n");
    RTOS_EXIT_CRITICAL();

    rtos_task_create(task_fp_a, "fp_a", task_fp_a_stack, 256, NULL, 2, NULL);
    rtos_task_create(task_fp_b, "fp_b", task_fp_b_stack, 256, NULL, 1, NULL);
    rtos_task_create(task_intr, "intr", task_intr_stack, 128, NULL, 3, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_FPU */
