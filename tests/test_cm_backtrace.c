/*
 * Test: CmBacktrace integration
 *
 * 验证项：
 *  - cm_backtrace_init 初始化成功
 *  - cm_backtrace_firmware_info 输出固件信息
 *  - cm_backtrace_call_stack 在正常状态下获取函数调用栈
 *  - 制造除零 HardFault，验证 CmBacktrace 自动诊断与调用栈回溯输出
 *
 * 注意：本测试触发 HardFault 后系统会停止在 Fault_Loop，需通过串口观察输出。
 */

#include "linRTOS.h"

#ifdef TEST_CM_BACKTRACE

#include "cm_backtrace.h"

extern void debug_printf(const char *fmt, ...);

/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t test_task_stack[512];

/* ============================================================
 * 制造非法内存访问异常（野指针写）
 * ============================================================ */

static void __attribute__((noinline)) trigger_memory_fault(void)
{
    /* 向一个未映射地址写入，触发 BusFault -> HardFault。
     * STM32 启动后地址 0x00000000 映射到 Flash（有效区域），写它不会触发异常，
     * 因此使用 0xFFFFFFFF（确定未映射）作为野指针目标。
     * 使用 volatile 指针防止编译器优化掉写操作。
     */
    volatile uint32_t *bad_ptr = (volatile uint32_t *)0xFFFFFFFF;
    *bad_ptr = 0xDEADBEEF;
}

/* 嵌套调用以增加调用栈深度 */
static void __attribute__((noinline)) level3(void)
{
    debug_printf("[CMB] About to trigger HardFault (illegal memory access)...\r\n");
    trigger_memory_fault();
}

static void __attribute__((noinline)) level2(void)
{
    level3();
}

static void __attribute__((noinline)) level1(void)
{
    level2();
}

/* ============================================================
 * CmBacktrace 测试任务
 * ============================================================ */

static void cm_backtrace_test_task(void *param)
{
    (void)param;

    /* 初始化 CmBacktrace */
    cm_backtrace_init(NULL, "v1.0", "v1.0");

    /* 打印固件信息 */
    cm_backtrace_firmware_info();

    /* 在正常状态下获取当前调用栈 */
    {
        uint32_t call_stack[16] = {0};
        size_t depth;
        uint32_t sp = cmb_get_sp();

        depth = cm_backtrace_call_stack(call_stack, sizeof(call_stack) / sizeof(call_stack[0]), sp);
        debug_printf("[CMB] Current call stack depth = %u\r\n", (unsigned)depth);
        for (size_t i = 0; i < depth; i++) {
            debug_printf("[CMB]   [%u] 0x%08X\r\n", (unsigned)i, (unsigned)call_stack[i]);
        }
    }

    /* 触发 HardFault，验证 CmBacktrace 故障追踪 */
    level1();

    /* 不会到达这里 */
    for (;;) {
        rtos_task_delay(1000);
    }
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    debug_printf("=== Test: CmBacktrace Integration ===\r\n");

    rtos_task_create(cm_backtrace_test_task, "cmb_test",
                     test_task_stack, sizeof(test_task_stack) / sizeof(uint32_t),
                     NULL, 2, NULL);

    rtos_task_delete(NULL);
}

#endif /* TEST_CM_BACKTRACE */
