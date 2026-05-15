/*
 * LinRTOS QEMU Test — Preemption
 * High-priority task periodically preempts a low-priority busy-loop task.
 */

#include "rtos.h"
#include <stdint.h>

static uint32_t high_stack[128];
static uint32_t low_stack[128];

static volatile int high_runs = 0;
static volatile int test_done = 0;

static void sh_puts(const char *s)
{
    __asm volatile (
        "movs r0, #0x04\n"
        "mov  r1, %0\n"
        "bkpt #0xAB\n"
        : : "r" (s) : "r0", "r1", "memory"
    );
}

static void sh_exit(int code)
{
    __asm volatile (
        "movs r0, #0x18\n"
        "mov  r1, %0\n"
        "bkpt #0xAB\n"
        : : "r" (code) : "r0", "r1", "memory"
    );
}

static void high_task(void *param)
{
    (void)param;
    for (int i = 0; i < 3; i++) {
        high_runs++;
        rtos_task_delay(3);
    }
    test_done = 1;
    sh_puts("PASS: preemption\n");
    sh_exit(0);
}

static void low_task(void *param)
{
    (void)param;
    while (!test_done) {
        /* busy loop, should be preempted by high_task */
        __asm volatile ("nop");
    }
}

int main(void)
{
    rtos_task_create(high_task, "high", high_stack, 128, NULL, 2, NULL);
    rtos_task_create(low_task,  "low",  low_stack,  128, NULL, 1, NULL);
    rtos_scheduler_start();
    sh_exit(1);
    return 0;
}
