/*
 * Test: CmBacktrace integration (conditional: COMPONENT_CM_BACKTRACE)
 * 验证: 初始化, 固件信息输出, 正常状态下调用栈回溯深度 > 0
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_CM_BACKTRACE) && defined(ARCH_COMPILER_GCC)

#ifdef COMPONENT_CM_BACKTRACE
#include "cm_backtrace.h"
#endif

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

#ifdef COMPONENT_CM_BACKTRACE
static bool test_cm_backtrace(void)
{

    cm_backtrace_init("LinRTOS-test", "hw-v1.0", "sw-v1.0");
    cm_backtrace_firmware_info();

    uint32_t call_stack[16] = {0};
    size_t depth;
    uint32_t sp = cmb_get_sp();

    depth = cm_backtrace_call_stack(call_stack,
                                     sizeof(call_stack) / sizeof(call_stack[0]),
                                     sp);
    sys_printk("  call stack depth=%u\r\n", (unsigned)depth);
    for (size_t i = 0; i < depth; i++) {
        sys_printk("  [%u] 0x%08X\r\n", (unsigned)i, (unsigned)call_stack[i]);
    }

    TEST_ASSERT(depth > 0, "call stack depth should be > 0");
    return true;
}
#else
static bool test_cm_backtrace(void)
{
    sys_printk("  SKIP\n");
    return true;
}
#endif

TEST_CASE_REGISTER(cm_backtrace, test_cm_backtrace);

#endif
