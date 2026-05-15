/*
 * LinRTOS QEMU Test — Task self-delete and remote delete
 */

#include "rtos.h"
#include <stdint.h>

static uint32_t worker_stack[128];
static uint32_t killer_stack[128];
static rtos_task_handle_t worker_handle;
static volatile int worker_done = 0;

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

static void worker_task(void *param)
{
    (void)param;
    worker_done = 1;
    /* Self-delete */
    rtos_task_delete(NULL);
    /* Should never reach here */
    sh_puts("FAIL: after self-delete\n");
    sh_exit(1);
}

static void killer_task(void *param)
{
    (void)param;
    /* Wait until worker has finished */
    while (!worker_done) {
        __asm volatile ("nop");
    }
    rtos_task_delay(3);
    /* Worker already deleted itself; this is a no-op but should not crash */
    rtos_task_delete(worker_handle);
    sh_puts("PASS: task delete\n");
    sh_exit(0);
}

int main(void)
{
    rtos_task_create(worker_task, "worker", worker_stack, 128, NULL, 2, &worker_handle);
    rtos_task_create(killer_task, "killer", killer_stack, 128, NULL, 1, NULL);
    rtos_scheduler_start();
    sh_exit(1);
    return 0;
}
