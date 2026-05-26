/*
 * LinCLI - LinRTOS built-in task integration.
 * Creates the CLI task automatically inside rtos_scheduler_start(),
 * just like the idle task.
 */

#include "linRTOS.h"
#include "cli_io.h"

extern int scheduler_init(void);
extern int scheduler_task(void);

#ifndef LINCLI_STACK_SIZE
#define LINCLI_STACK_SIZE 256
#endif

static uint32_t s_lincli_stack[LINCLI_STACK_SIZE];

static void lincli_task_entry(void *param)
{
    (void)param;
    if (scheduler_init() < 0) {
        return;
    }
    for (;;) {
        scheduler_task();
        rtos_task_delay(20);
    }
}

/* Called from rtos_scheduler_start() via weak symbol override */
void rtos_lincli_init(void)
{
    rtos_task_create(lincli_task_entry, "lincli",
                     s_lincli_stack,
                     sizeof(s_lincli_stack) / sizeof(uint32_t),
                     NULL, 31, NULL);
}
