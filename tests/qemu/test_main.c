/*
 * LinRTOS - QEMU integration test (mps2-an385, semihosting).
 */

#include "rtos.h"
#include <stdint.h>

static uint32_t task1_stack[128];
static uint32_t task2_stack[128];

static volatile int test_passed = 0;
static volatile int sem_count = 0;

static rtos_sem_handle_t sem;
static rtos_mutex_handle_t mutex;

/* ============================================================
 * 🖥️ Semihosting helpers
 * ============================================================ */

static void sh_puts(const char *s)
{
    __asm volatile (
        "movs r0, #0x04\n"   /* SYS_WRITE0 */
        "mov  r1, %0\n"
        "bkpt #0xAB\n"
        :
        : "r" (s)
        : "r0", "r1", "memory"
    );
}

static void sh_exit(int code)
{
    __asm volatile (
        "movs r0, #0x18\n"   /* SYS_EXIT */
        "mov  r1, %0\n"
        "bkpt #0xAB\n"
        :
        : "r" (code)
        : "r0", "r1", "memory"
    );
}

/* ============================================================
 * 📋 Tasks
 * ============================================================ */

static void task1_func(void *param)
{
    (void)param;
    sh_puts("[task1] started\n");

    for (int i = 0; i < 5; i++) {
        rtos_mutex_take(mutex, RTOS_WAIT_FOREVER);
        sem_count++;
        rtos_mutex_give(mutex);

        rtos_sem_give(sem);
        rtos_task_delay(10);
    }

    test_passed = 1;
    sh_puts("[task1] test passed, exiting\n");
    sh_exit(0);
}

static void task2_func(void *param)
{
    (void)param;
    sh_puts("[task2] started\n");

    while (!test_passed) {
        rtos_sem_take(sem, RTOS_WAIT_FOREVER);
        rtos_mutex_take(mutex, RTOS_WAIT_FOREVER);
        int cnt = sem_count;
        rtos_mutex_give(mutex);
        (void)cnt;
    }

    sh_puts("[task2] done\n");
}

/* ============================================================
 * 🎬 Main
 * ============================================================ */

int main(void)
{
    sh_puts("\n=== LinRTOS QEMU Test ===\n");

    rtos_sem_create(&sem, 0);
    rtos_mutex_create(&mutex);

    rtos_task_create(task1_func, "producer",
                     task1_stack, sizeof(task1_stack) / sizeof(uint32_t),
                     NULL, 2, NULL);

    rtos_task_create(task2_func, "consumer",
                     task2_stack, sizeof(task2_stack) / sizeof(uint32_t),
                     NULL, 1, NULL);

    sh_puts("[main] starting scheduler\n");
    rtos_scheduler_start();

    /* never reached */
    sh_puts("[main] scheduler returned! FAIL\n");
    sh_exit(1);
    return 0;
}
