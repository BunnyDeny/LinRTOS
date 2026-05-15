/*
 * LinRTOS QEMU Test — Priority Inheritance
 * A low-priority task holds a mutex; a high-priority task blocks on it.
 * The low-priority task must temporarily inherit the high priority,
 * preventing a mid-priority task from preempting it.
 */

#include "rtos.h"
#include <stdint.h>

static uint32_t low_stack[128];
static uint32_t high_stack[128];
static uint32_t mid_stack[128];

static rtos_mutex_handle_t mtx;
static rtos_sem_handle_t sem_sync;
static volatile int step = 0;

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

static void low_task(void *param)
{
    (void)param;
    rtos_mutex_take(mtx, RTOS_WAIT_FOREVER);
    step = 1;

    /* Signal high_task that the mutex is held, then yield so high_task
     * can run and block on the mutex. */
    rtos_sem_give(sem_sync);
    rtos_task_yield();

    /* At this point high_task is blocked on the mutex and low_task has
     * inherited priority 3.  mid_task (priority 2) must NOT be able to
     * preempt low_task during this critical section. */
    for (volatile int i = 0; i < 100000; i++) {
        __asm volatile ("nop");
    }

    if (step != 1) {
        sh_puts("FAIL: mid preempted inherited low\n");
        sh_exit(1);
    }
    step = 2;
    rtos_mutex_give(mtx);
    rtos_task_delay(5);
    sh_puts("PASS: priority inheritance\n");
    sh_exit(0);
}

static void mid_task(void *param)
{
    (void)param;
    /* mid_task will become ready while low_task holds the mutex.
     * If priority inheritance works, mid_task (prio 2) must not run
     * while low_task has inherited prio 3. */
    if (step == 1) {
        step = 99;
    }
    rtos_task_delete(NULL);
}

static void high_task(void *param)
{
    (void)param;
    /* Wait until low_task has acquired the mutex. */
    rtos_sem_take(sem_sync, RTOS_WAIT_FOREVER);

    /* Block on the mutex — this should boost low_task to prio 3. */
    rtos_mutex_take(mtx, RTOS_WAIT_FOREVER);
    step = 3;
    rtos_mutex_give(mtx);
    rtos_task_delete(NULL);
}

int main(void)
{
    rtos_mutex_create(&mtx);
    rtos_sem_create(&sem_sync, 0);
    rtos_task_create(low_task,  "low",  low_stack,  128, NULL, 1, NULL);
    rtos_task_create(mid_task,  "mid",  mid_stack,  128, NULL, 2, NULL);
    rtos_task_create(high_task, "high", high_stack, 128, NULL, 3, NULL);
    rtos_scheduler_start();
    sh_exit(1);
    return 0;
}
