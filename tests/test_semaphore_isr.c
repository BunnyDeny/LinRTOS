/*
 * Test: Semaphore ISR — give_from_isr → task take
 * 验证: ISR 通过 give_from_isr 释放信号量，任务端阻塞等待，高优先级唤醒标志
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "rtos_semaphore.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_SEMAPHORE_ISR) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];

extern struct rtos_queue s_isr_sem;
extern volatile uint32_t s_isr_sem_sent;
extern volatile bool     s_isr_sem_done;
extern volatile bool     s_isr_sem_woken;
extern volatile bool     s_isr_sem_active;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { sys_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_semaphore_isr(void)
{
    static volatile bool cons_done = false;


    s_isr_sem_sent  = 0;
    s_isr_sem_done  = false;
    s_isr_sem_woken = false;
    cons_done       = false;

    void consumer(void *p) {
        (void)p;
        for (uint32_t i = 0; i < 5; i++) {
            rtos_err_t e = rtos_semaphore_take(&s_isr_sem, RTOS_WAIT_FOREVER);
            if (e == RTOS_OK) {
                sys_printk("  sem-isr take %lu\r\n", (unsigned long)(i + 1));
            }
        }
        s_isr_sem_done = true;
        cons_done = true;
        rtos_task_delete(NULL);
    }

    rtos_semaphore_init_binary(&s_isr_sem);
    rtos_task_create(consumer, "scons", s_stk0, 160, NULL, 5, NULL);

    s_isr_sem_active = true;

    uint32_t start = rtos_get_tick_count();
    while (!cons_done) {
        rtos_task_delay(100);
        if ((int32_t)(rtos_get_tick_count() - start) > 5000) break;
    }
    s_isr_sem_active = false;

    TEST_ASSERT(cons_done, "consumer should have taken all 5");
    TEST_ASSERT(s_isr_sem_sent >= 5, "ISR should have given >= 5 times");
    TEST_ASSERT(s_isr_sem_woken, "ISR should have woken a task");

    rtos_semaphore_delete(&s_isr_sem);
    return true;
}

TEST_CASE_REGISTER(semaphore_isr, test_semaphore_isr);

#endif
