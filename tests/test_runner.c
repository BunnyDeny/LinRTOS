/*
 * LinRTOS - Centralized test runner.
 *
 * Provides:
 *  - app_entry_task          entry-point that iterates the .test_cases section
 *  - SysTick_Handler         unified ISR handler with workqueue and ISR-test hooks
 *  - Shared stack pool       reused across all tests (they run sequentially)
 *  - ISR test infrastructure queues / semaphores / active flags
 *  - wait_for / wait_for_bool helpers
 *
 * Individual test files just register via TEST_CASE_REGISTER(name, fn).
 */

#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_mutex.h"
#include "rtos_semaphore.h"
#include "test_case.h"

#ifdef WORKQUEUE
#include "workqueue.h"
#endif

#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_RESET  "\033[0m"

#if defined(ENABLE_TEST_CASES) && defined(ARCH_COMPILER_GCC)

/* ============================================================
 * Section boundary markers
 * ============================================================ */

const test_case_t *const _test_cases_start[1]
    __attribute__((used, section(".test_cases.0.start"))) = { NULL };
const test_case_t *const _test_cases_end[1]
    __attribute__((used, section(".test_cases.1.end"))) = { NULL };

/* ============================================================
 * Shared stack pool (tests run sequentially — safe to reuse)
 * ============================================================ */

uint32_t s_stk0[160];
uint32_t s_stk1[160];
uint32_t s_stk2[160];
uint32_t s_stk3[160];
#ifdef ARCH_ENABLE_FPU
uint32_t s_fpu_stk0[256];
uint32_t s_fpu_stk1[256];
uint32_t s_fpu_stk2[160];
#endif

/* ============================================================
 * ISR test infrastructure
 * ============================================================ */

/* ---- Queue ISR ---- */
struct rtos_queue s_isr_q;
uint8_t  s_isr_q_buf[5 * sizeof(uint32_t)];
volatile uint32_t s_isr_q_sent  = 0;
volatile bool     s_isr_q_done  = false;
volatile bool     s_isr_q_woken = false;

/* ---- Semaphore ISR ---- */
struct rtos_queue s_isr_sem;
volatile uint32_t s_isr_sem_sent  = 0;
volatile bool     s_isr_sem_done  = false;
volatile bool     s_isr_sem_woken = false;

/* ---- Active flags (set by ISR test functions) ---- */
volatile bool s_isr_queue_active = false;
volatile bool s_isr_sem_active   = false;
volatile bool s_suite_done       = false;

/* ============================================================
 * Helpers
 * ============================================================ */

static __attribute__((unused)) bool wait_for(volatile uint32_t *flag, uint32_t expect,
                     uint32_t timeout_ticks)
{
    uint32_t start = rtos_get_tick_count();
    while (*flag < expect) {
        if ((int32_t)(rtos_get_tick_count() - start) >= (int32_t)timeout_ticks) {
            sys_printk("  FAIL L%d: timeout waiting flag=%lu expect=%lu\r\n",
                       __LINE__, (unsigned long)*flag, (unsigned long)expect);
            return false;
        }
        rtos_task_delay(10);
    }
    return true;
}

static __attribute__((unused)) bool wait_for_bool(volatile bool *flag, bool expect,
                          uint32_t timeout_ticks)
{
    uint32_t start = rtos_get_tick_count();
    while (*flag != expect) {
        if ((int32_t)(rtos_get_tick_count() - start) >= (int32_t)timeout_ticks) {
            sys_printk("  FAIL L%d: timeout waiting bool\r\n", __LINE__);
            return false;
        }
        rtos_task_delay(10);
    }
    return true;
}

/* ============================================================
 * SysTick_Handler — unified ISR entry
 * ============================================================ */

void SysTick_Handler(void)
{
    rtos_tick_handler();

    if (s_suite_done) return;

#ifdef WORKQUEUE
    if (system_wq) {
        workqueue_tick_handler(system_wq, rtos_get_tick_count());
        workqueue_run_one(system_wq);
    }
#endif

    static uint32_t isr_cnt = 0;
    isr_cnt++;

    /* ---- Queue ISR ---- */
    if (s_isr_queue_active && (isr_cnt % 200 == 0) && !s_isr_q_done) {
        uint32_t d = isr_cnt;
        bool hp = false;
        if (rtos_queue_generic_send_from_isr(&s_isr_q, &d,
                                              RTOS_QUEUE_SEND_BACK,
                                              &hp) == RTOS_OK) {
            s_isr_q_sent++;
            if (hp) s_isr_q_woken = true;
        }
    }

    /* ---- Semaphore ISR ---- */
    if (s_isr_sem_active && (isr_cnt % 200 == 0) && !s_isr_sem_done) {
        bool hp = false;
        if (rtos_semaphore_give_from_isr(&s_isr_sem, &hp) == RTOS_OK) {
            s_isr_sem_sent++;
            if (hp) s_isr_sem_woken = true;
        }
    }
}

/* ============================================================
 * Test runner entry
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    sys_printk("=== LinRTOS Test Suite ===\r\n");

    /* First pass: count registered tests */
    int total = 0;
    {
        const test_case_t *dummy;
        FOR_EACH_TEST_CASE(dummy) { total++; }
    }

    if (total == 0) {
        sys_printk("  No test cases registered.\r\n");
        s_suite_done = true;
        rtos_task_delete(NULL);
        return;
    }

    int pass = 0;
    int idx  = 0;
    const test_case_t *tc;

    FOR_EACH_TEST_CASE(tc) {
        idx++;
        bool ok = tc->fn();
        if (ok) {
            pass++;
            sys_printk("[ " COLOR_GREEN "ok" COLOR_RESET " ] [%2d/%2d] %s\r\n",
                       idx, total, tc->name);
        } else {
            sys_printk("[ " COLOR_RED "err" COLOR_RESET " ] [%2d/%2d] %s\r\n",
                       idx, total, tc->name);
        }
        rtos_task_delay(50);
    }

    sys_printk("\r\n========================================\r\n");
    sys_printk("  Results: %d / %d PASS\r\n", pass, total);
    sys_printk("========================================\r\n");

    s_suite_done = true;
    rtos_task_delete(NULL);
}

#endif /* ENABLE_TEST_CASES */
