/*
 * LinRTOS - Simple two-task demo with semaphore and mutex.
 *
 * Build for MCU:
 *   arm-none-eabi-gcc -mthumb -mcpu=cortex-m4 -I../include \
 *     ../src/*.c ../src/port/cortex_m/*.S \
 *     simple_demo.c -T your_linker_script.ld -o simple_demo.elf
 */

#include "rtos.h"
#include <stdint.h>

/* ============================================================
 * 🪶 任务栈
 * ============================================================ */

static uint32_t task1_stack[128];
static uint32_t task2_stack[128];

/* ============================================================
 * 🔧 同步对象
 * ============================================================ */

static rtos_sem_handle_t sem;
static rtos_mutex_handle_t mutex;

static volatile uint32_t shared_counter = 0;

/* ============================================================
 * 📋 任务 1：高优先级，周期性生产
 * ============================================================ */

static void task1_func(void *param)
{
    (void)param;
    for (;;) {
        rtos_mutex_take(mutex, RTOS_WAIT_FOREVER);
        shared_counter++;
        rtos_mutex_give(mutex);

        rtos_sem_give(sem);
        rtos_task_delay(100);   /* 每 100 tick 生产一次 */
    }
}

/* ============================================================
 * 📋 任务 2：低优先级，消费
 * ============================================================ */

static void task2_func(void *param)
{
    (void)param;
    for (;;) {
        rtos_sem_take(sem, RTOS_WAIT_FOREVER);

        rtos_mutex_take(mutex, RTOS_WAIT_FOREVER);
        uint32_t val = shared_counter;
        rtos_mutex_give(mutex);

        /* 这里可以读取 val 做业务处理 */
        (void)val;

        rtos_task_delay(50);
    }
}

/* ============================================================
 * 🎬 入口
 * ============================================================ */

int main(void)
{
    /* 创建同步对象 */
    rtos_sem_create(&sem, 0);
    rtos_mutex_create(&mutex);

    /* 创建任务（prio 数值越大优先级越高） */
    rtos_task_create(task1_func, "producer",
                     task1_stack, sizeof(task1_stack) / sizeof(uint32_t),
                     NULL, 2, NULL);

    rtos_task_create(task2_func, "consumer",
                     task2_stack, sizeof(task2_stack) / sizeof(uint32_t),
                     NULL, 1, NULL);

    /* 启动调度器（不会返回） */
    rtos_scheduler_start();

    return 0;
}
