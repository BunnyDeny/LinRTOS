/*
 * Test: CmBacktrace integration (conditional: COMPONENT_CM_BACKTRACE)
 *
 * 直接在 test_cm_backtrace() 中完成:
 *   1. 初始化 + 正常状态调用栈
 *   2. 触发 HardFault → CmBacktrace 自动诊断
 *
 * 不创建独立任务, 零 TCB 依赖, 避免跨测试状态干扰.
 * HardFault 后系统停止, 本测试应单独选中或放最后.
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_CM_BACKTRACE) && defined(ARCH_COMPILER_GCC)

#ifdef COMPONENT_CM_BACKTRACE
#include "cm_backtrace.h"

static void __attribute__((noinline)) trigger_memory_fault(void)
{
    cli_printk("[CMB] Triggering illegal memory access...\r\n");
    volatile uint32_t *bad_ptr = (volatile uint32_t *)0xFFFFFFFF;
    *bad_ptr = 0xDEADBEEF;
}

static void __attribute__((noinline)) level3(void)
{
    cli_printk("[CMB] About to trigger HardFault...\r\n");
    trigger_memory_fault();
}

static void __attribute__((noinline)) level2(void) { level3(); }
static void __attribute__((noinline)) level1(void) { level2(); }

static bool test_cm_backtrace(void)
{
    cli_printk("\r\n  -> HardFault will be triggered, system halts.\r\n");

    cm_backtrace_init("LinRTOS-test", "hw-v1.0", "sw-v1.0");
    cm_backtrace_firmware_info();

    /* normal state: dump call stack */
    {
        uint32_t call_stack[16] = {0};
        size_t depth;
        uint32_t sp = cmb_get_sp();

        /* debug: print stack boundaries being scanned */
        extern uint32_t _stext, _etext;
        cli_printk("[CMB] code: %08x-%08x sp=%08x psp=%08x on_psp=%d\r\n",
                   (unsigned)&_stext, (unsigned)&_etext,
                   (unsigned)sp, (unsigned)cmb_get_psp(), cmb_is_on_psp());

        depth = cm_backtrace_call_stack(call_stack,
                                         sizeof(call_stack) / sizeof(call_stack[0]), sp);
        cli_printk("[CMB] call stack depth=%u\r\n", (unsigned)depth);
        for (size_t i = 0; i < depth; i++) {
            cli_printk("[CMB]   [%u] 0x%08X\r\n", (unsigned)i, (unsigned)call_stack[i]);
        }
    }

    level1();

    for (;;) {}
    return true;
}

#else
static bool test_cm_backtrace(void)
{
    cli_printk("  SKIP\n");
    return true;
}
#endif

TEST_CASE_REGISTER(cm_backtrace, test_cm_backtrace);

#endif
