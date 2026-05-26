/*
 * Test: Task state query
 *
 * 验证项：
 *  - rtos_task_get_state    查询任务状态（RUNNING / READY / BLOCKED / DELETED）
 *  - rtos_task_get_priority 查询任务优先级
 *  - rtos_task_get_current  获取当前任务句柄
 */

#include "linRTOS.h"
#include "cli_io.h"

#ifdef TEST_STATE


/* ============================================================
 * 静态资源
 * ============================================================ */

static uint32_t task_query_stack[128];
static uint32_t task_helper_stack[128];

static rtos_task_handle_t h_helper = NULL;

/* ============================================================
 * 查询任务 —— 查询自身与 helper 的状态
 * ============================================================ */

static void task_query(void *param)
{
    (void)param;

    /* 查询自身状态（应为 RUNNING） */
    rtos_task_state_t self_state = rtos_task_get_state(NULL);
    uint32_t self_prio = rtos_task_get_priority(NULL);
    rtos_task_handle_t self = rtos_task_get_current();
    pr_debug("[QRY ] self  state=%d prio=%lu me=%p\r\n",
                     (int)self_state, (unsigned long)self_prio, (void *)self);

    /* 查询 helper 状态（应为 BLOCKED，因 helper 正在 delay） */
    rtos_task_state_t helper_state = rtos_task_get_state(h_helper);
    pr_debug("[QRY ] helper state=%d (expected BLOCKED=2)\r\n",
                     (int)helper_state);

    rtos_task_delay(500);

    /* 再次查询 helper，仍在 delay 中 */
    helper_state = rtos_task_get_state(h_helper);
    pr_debug("[QRY ] helper state=%d after 500 ticks\r\n",
                     (int)helper_state);

        pr_debug("[QRY ] test done, entering idle\r\n");
    for (;;) {
        rtos_task_delay(1000);
    }
}

/* ============================================================
 * 辅助任务 —— 长时间 delay，供查询任务观察其 BLOCKED 状态
 * ============================================================ */

static void task_helper(void *param)
{
    (void)param;
    for (;;) {
        rtos_task_delay(10000);
    }
}

/* ============================================================
 * 统一入口
 * ============================================================ */

void app_entry_task(void *param)
{
    (void)param;

    pr_debug("=== Test: State Query ===\r\n");

    rtos_task_create(task_query,  "query",  task_query_stack,  128, NULL, 2, NULL);
    rtos_task_create(task_helper, "helper", task_helper_stack, 128, NULL, 1, &h_helper);

    rtos_task_delete(NULL);
}

#endif /* TEST_STATE */
