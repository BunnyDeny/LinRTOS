/*
 * Test: Task suspend and resume
 *
 * 占位文件，后续实现。
 */

#include "linRTOS.h"

#ifdef TEST_SUSPEND_RESUME


extern void debug_printf(const char *fmt, ...);

void app_entry_task(void *param)
{
    (void)param;
    debug_printf("=== Test: Suspend/Resume (TODO) ===\r\n");
    /* TODO: implement suspend/resume tests */
    rtos_task_delete(NULL);
}

#endif /* TEST_SUSPEND_RESUME */
