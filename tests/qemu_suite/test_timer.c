/*
 * LinRTOS QEMU Test — Software Timer
 * Create a periodic timer and verify it fires expected number of times.
 */

#include "rtos.h"
#include <stdint.h>

static uint32_t task_stack[128];
static rtos_timer_handle_t timer;
static volatile int timer_count = 0;

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

static void timer_callback(rtos_timer_handle_t tm)
{
    (void)tm;
    timer_count++;
}

static void task_func(void *param)
{
    (void)param;
    rtos_task_delay(25);
    if (timer_count >= 3 && timer_count <= 7) {
        sh_puts("PASS: timer\n");
        sh_exit(0);
    } else {
        sh_puts("FAIL: timer count wrong\n");
        sh_exit(1);
    }
}

int main(void)
{
    rtos_timer_create(&timer, "tm", 5, RTOS_TIMER_AUTO_RELOAD,
                      NULL, timer_callback);
    rtos_timer_start(timer);
    rtos_task_create(task_func, "t", task_stack, 128, NULL, 1, NULL);
    rtos_scheduler_start();
    sh_exit(1);
    return 0;
}
