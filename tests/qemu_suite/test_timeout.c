/*
 * LinRTOS QEMU Test — Semaphore / Mutex timeout
 * Verify that blocking with a timeout returns RTOS_ERR_TIMEOUT.
 */

#include "rtos.h"
#include <stdint.h>

static uint32_t task_stack[128];
static uint32_t holder_stack[128];
static rtos_sem_handle_t sem;
static rtos_mutex_handle_t mtx;

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

static void holder_func(void *param)
{
    (void)param;
    /* Lock the mutex and sleep long enough for the test task to time out */
    rtos_mutex_take(mtx, RTOS_WAIT_FOREVER);
    rtos_task_delay(100);
    /* Never reached in normal test flow */
    while (1) {
        rtos_task_yield();
    }
}

static void task_func(void *param)
{
    (void)param;
    rtos_err_t r1 = rtos_sem_take(sem, 3);
    rtos_err_t r2 = rtos_mutex_take(mtx, 3);
    if (r1 == RTOS_ERR_TIMEOUT && r2 == RTOS_ERR_TIMEOUT) {
        sh_puts("PASS: timeout\n");
        sh_exit(0);
    } else {
        sh_puts("FAIL: timeout\n");
        sh_exit(1);
    }
}

int main(void)
{
    rtos_sem_create(&sem, 0);
    rtos_mutex_create(&mtx);
    /* Holder runs first (higher priority), locks mutex, then delays */
    rtos_task_create(holder_func, "h", holder_stack, 128, NULL, 2, NULL);
    /* Test task runs when holder is delayed, tries to take held mutex */
    rtos_task_create(task_func, "t", task_stack, 128, NULL, 1, NULL);
    rtos_scheduler_start();
    sh_exit(1);
    return 0;
}
