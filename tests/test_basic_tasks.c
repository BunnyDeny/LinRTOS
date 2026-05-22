/*
 * Test: Basic multi-task + delay
 *
 * 占位文件，后续实现。
 */

#include "linRTOS.h"

#ifdef TEST_BASIC_TASKS


extern void debug_printf(const char *fmt, ...);

void app_entry_task(void *param)
{
    (void)param;
    debug_printf("=== Test: Basic Tasks (TODO) ===\r\n");
    /* TODO: implement basic task tests */
    rtos_task_delete(NULL);
}

#endif /* TEST_BASIC_TASKS */
