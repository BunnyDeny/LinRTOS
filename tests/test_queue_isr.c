/*
 * Test: Queue ISR — send_from_isr → task consume
 * 验证: ISR 通过 send_from_isr 发送数据，任务端阻塞接收，高优先级唤醒标志
 */
#include "linRTOS.h"
#include "cli_io.h"
#include "test_case.h"

#if defined(ENABLE_TEST_CASES) && defined(TEST_QUEUE_ISR) && defined(ARCH_COMPILER_GCC)

extern uint32_t s_stk0[160];

extern struct rtos_queue s_isr_q;
extern uint8_t  s_isr_q_buf[];
extern volatile uint32_t s_isr_q_sent;
extern volatile bool     s_isr_q_done;
extern volatile bool     s_isr_q_woken;
extern volatile bool     s_isr_queue_active;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { cli_printk("  FAIL L%d: %s\r\n", __LINE__, msg); return false; } \
} while (0)

static bool test_queue_isr(void)
{
    static volatile bool cons_done = false;


    s_isr_q_sent  = 0;
    s_isr_q_done  = false;
    s_isr_q_woken = false;
    cons_done     = false;

    void consumer(void *p) {
        (void)p;
        for (uint32_t i = 0; i < 8; i++) {
            uint32_t v;
            rtos_err_t e = rtos_queue_recv(&s_isr_q, &v, RTOS_WAIT_FOREVER);
            if (e == RTOS_OK) {
                cli_printk("  q-isr recv %lu\r\n", (unsigned long)v);
            }
        }
        s_isr_q_done = true;
        cons_done = true;
        rtos_task_delete(NULL);
    }

    rtos_queue_init(&s_isr_q, s_isr_q_buf, 5, sizeof(uint32_t));
    rtos_task_create(consumer, "qcons", s_stk0, 160, NULL, 5, NULL);

    s_isr_queue_active = true;

    uint32_t start = rtos_get_tick_count();
    while (!cons_done) {
        rtos_task_delay(100);
        if ((int32_t)(rtos_get_tick_count() - start) > 5000) break;
    }
    s_isr_queue_active = false;

    TEST_ASSERT(cons_done, "consumer should have received all 8 items");
    TEST_ASSERT(s_isr_q_sent >= 8, "ISR should have sent >= 8 items");
    TEST_ASSERT(s_isr_q_woken, "ISR should have woken a task");

    rtos_queue_delete(&s_isr_q);
    return true;
}

TEST_CASE_REGISTER(queue_isr, test_queue_isr);

#endif
