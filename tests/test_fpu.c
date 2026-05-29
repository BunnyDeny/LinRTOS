/*
 * Test: FPU context switch (conditional: ARCH_ENABLE_FPU)
 * 验证: 多浮点任务抢占调度时 FPU 寄存器正确保存/恢复, 浮点精度正确
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_FPU)

#ifdef ARCH_ENABLE_FPU
#include <math.h>
#endif

extern uint32_t s_stk0[160];
extern uint32_t s_stk1[160];
#ifdef ARCH_ENABLE_FPU
extern uint32_t s_fpu_stk0[256];
extern uint32_t s_fpu_stk1[256];
extern uint32_t s_fpu_stk2[160];
#endif

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

#ifdef ARCH_ENABLE_FPU
static bool test_fpu(void)
{
    static volatile uint32_t fp_cycles = 0;
    static volatile float fp_result_a = 0.0f;
    static volatile float fp_result_b = 0.0f;

    sys_printk("[%s]\r\n", __func__);
    fp_cycles = 0; fp_result_a = 0.0f; fp_result_b = 0.0f;

    void fp_task_a(void *p) {
        (void)p;
        float s = 0.0f;
        for (int i = 0; i < 10; i++) {
            s += 0.1f;
            (void)sinf(s);
            fp_cycles++;
            rtos_task_delay(20);
        }
        fp_result_a = s;  /* expected = 1.0f */
        rtos_task_delete(NULL);
    }

    void fp_task_b(void *p) {
        (void)p;
        float v = 1.0f;
        for (int i = 0; i < 10; i++) {
            v = v * 1.5f + 0.1f;
            fp_cycles++;
            rtos_task_delay(25);
        }
        fp_result_b = v;
        rtos_task_delete(NULL);
    }

    void intruder(void *p) {
        (void)p;
        for (int i = 0; i < 15; i++) {
            volatile float x = 0.5f;
            (void)x;
            rtos_task_delay(15);
        }
        rtos_task_delete(NULL);
    }

    rtos_task_create(fp_task_a, "fpa", s_fpu_stk0, 256, NULL, 2, NULL);
    rtos_task_create(fp_task_b, "fpb", s_fpu_stk1, 256, NULL, 1, NULL);
    rtos_task_create(intruder,  "intr", s_fpu_stk2, 160, NULL, 3, NULL);

    rtos_task_delay(600);

    TEST_ASSERT(fp_cycles >= 15, "FP tasks should have made progress");

    float diff = fp_result_a - 1.0f;
    if (diff < 0.0f) diff = -diff;
    TEST_ASSERT(diff < 0.01f, "fp_task_a accuracy (sum=1.0)");

    TEST_ASSERT(fp_result_b > 1.0f, "fp_task_b should have made progress");

    return true;
}
#else
static bool test_fpu(void)
{
    sys_printk("[%s] SKIP (no FPU)\r\n", __func__);
    return true;
}
#endif

TEST_CASE_REGISTER(fpu, test_fpu);

#endif
