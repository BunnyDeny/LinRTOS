/*
 * Test: CmBacktrace integration (conditional: COMPONENT_CM_BACKTRACE)
 *
 * 用 noinline 函数构建 level1->level2->level3->capture 调用链,
 * 在最深层捕获回溯, 单次 sys_printk 打印完整 addr2line 命令.
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_CM_BACKTRACE) && defined(ARCH_COMPILER_GCC)

#ifdef COMPONENT_CM_BACKTRACE
#include "cm_backtrace.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static __attribute__((noinline)) void cmb_capture(volatile size_t *out_depth,
                                                   uint32_t *stack, size_t n)
{
    uint32_t sp = cmb_get_sp();
    *out_depth = cm_backtrace_call_stack(stack, n, sp);
}

static __attribute__((noinline)) void cmb_level3(volatile size_t *out_depth,
                                                   uint32_t *stack, size_t n)
{
    cmb_capture(out_depth, stack, n);
}

static __attribute__((noinline)) void cmb_level2(volatile size_t *out_depth,
                                                   uint32_t *stack, size_t n)
{
    cmb_level3(out_depth, stack, n);
}

static __attribute__((noinline)) void cmb_level1(volatile size_t *out_depth,
                                                   uint32_t *stack, size_t n)
{
    cmb_level2(out_depth, stack, n);
}

static bool test_cm_backtrace(void)
{
    cm_backtrace_init("LinRTOS-test", "hw-v1.0", "sw-v1.0");
    sys_printk("  Firmware: LinRTOS-test hw-v1.0 sw-v1.0\r\n");

    uint32_t call_stack[16] = {0};
    volatile size_t depth = 0;

    /* capture -> level3 -> level2 -> level1 -> test_cm_backtrace -> ... */
    cmb_level1(&depth, call_stack, sizeof(call_stack) / sizeof(call_stack[0]));

    /* build the complete addr2line line in a static buffer, one sys_printk */
    if (depth > 0) {
        static char line[256];
        int pos = 0;
        pos += snprintf(line + pos, sizeof(line) - pos,
                        "  addr2line -e stm32g431_gcc_example_project.elf -afpiC");
        for (size_t i = 0; i < depth && pos < (int)sizeof(line) - 12; i++) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            " %08lx", (unsigned long)call_stack[i]);
        }
        sys_printk("%s\r\n", line);
    }

    sys_printk("  call stack depth=%u\r\n", (unsigned)depth);

    TEST_ASSERT(depth >= 5, "depth should be >= 5 (multi-level call chain)");

    for (size_t i = 0; i < depth; i++) {
        uint32_t addr = call_stack[i];
        if (addr == 0) break;
        TEST_ASSERT(addr >= 0x08000000U && addr < 0x08020000U,
                    "return address should be in Flash range");
    }

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
