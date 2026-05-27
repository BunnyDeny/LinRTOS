/*
 * LinRTOS - Kernel internal definitions.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_KERNEL_H
#define RTOS_KERNEL_H

#include "types.h"
#include "rtos_list.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 📌 任务控制块 (TCB)
 * ============================================================ */

struct rtos_tcb {
    uint32_t *stack_ptr;           /* 💾 当前栈指针 (PSP) */
    uint32_t *stack_base;          /* 📍 栈底（最低地址） */
    uint32_t *stack_top;           /* 📍 栈顶（最高地址，初始化位置） */
    uint32_t  stack_size;          /* 📏 栈大小（字，uint32_t 个数） */
    uint32_t  priority;            /* 🔺 当前优先级（数值越大越高） */
    uint32_t  time_slice;          /* ⏱️ 剩余时间片 tick */
    rtos_task_state_t state;       /* 📊 当前状态 */
    char      name[RTOS_MAX_TASK_NAME_LEN];

    struct rtos_list_node ready_node;   /* 🔗 就绪队列链表节点 */
    struct rtos_list_node delay_node;   /* 🔗 延时队列链表节点 */
    struct rtos_list_node event_node;   /* 🔗 事件等待链表节点（队列/信号量阻塞用） */
    struct rtos_list_node *event_list;  /* 🔗 指向当前阻塞的事件链表头，NULL=未在事件上阻塞 */

    uint32_t  wake_tick;           /* ⏰ 唤醒时间（绝对tick） */
};

/* ============================================================
 * ⚙️ 就绪队列位图加速
 * ============================================================ */

#if RTOS_MAX_PRIORITIES <= 32
typedef uint32_t rtos_ready_map_t;
#define RTOS_READY_MAP_BITS     32
#elif RTOS_MAX_PRIORITIES <= 64
typedef uint64_t rtos_ready_map_t;
#define RTOS_READY_MAP_BITS     64
#else
#error "RTOS_MAX_PRIORITIES must be <= 64"
#endif

/* ============================================================
 * 🏭 内核全局状态
 * ============================================================ */

struct rtos_kernel {
    struct rtos_list_node ready_list[RTOS_MAX_PRIORITIES];  /* 各优先级就绪链表头 */
    rtos_ready_map_t ready_map;      /* 位图：1=该优先级有就绪任务 */

    struct rtos_tcb *idle_task;      /* 空闲任务 */

    volatile uint32_t tick_count;    /* 全局tick计数 */
    volatile uint32_t sched_lock;    /* 调度锁嵌套计数（>0 禁止抢占） */
    volatile uint8_t  is_running;    /* 调度器是否已启动 */
    volatile uint8_t  need_resched;  /* 标记需要调度 */

    struct rtos_list_node delay_list;           /* 当前周期的延时队列（按 wake_tick 升序） */
    struct rtos_list_node delay_list_overflow;  /* 跨越 tick 回绕边界的延时队列 */
    struct rtos_list_node *px_delayed_task_list;      /* 指向当前活跃的 delay 列表 */
    struct rtos_list_node *px_overflow_delayed_task_list; /* 指向 overflow 列表 */

    struct rtos_list_node terminated_list; /* 已终止、待空闲任务回收的 TCB 列表 */

#if RTOS_ENABLE_IDLE_HOOK
    rtos_idle_hook_t idle_hook;
#endif
};

/* 唯一全局内核实例 */
extern struct rtos_kernel g_kernel;
extern volatile struct rtos_tcb *rtos_current_tcb;

/* 内核一次性初始化（首次使用任何内核服务前自动调用） */
void rtos_kernel_init(void);

/* ============================================================
 * 🔧 内核内部函数（不对用户暴露）
 * ============================================================ */

/* 将任务插入就绪队列（按优先级） */
void rtos_task_ready(struct rtos_tcb *tcb);

/* 将任务从就绪队列移除 */
void rtos_task_unready(struct rtos_tcb *tcb);

/* 查找当前最高优先级的就绪任务 */
struct rtos_tcb *rtos_pick_highest_ready(void);

/* 触发一次调度（若调度器已运行且未上锁） */
void rtos_sched(void);

/* 请求调度（设置标志，在退出临界区或中断末尾触发） */
void rtos_sched_yield(void);

/* tick处理：更新延时队列 */
void rtos_tick_handler(void);

#if RTOS_ENABLE_TIME_SLICING
/*
 * ⚠️ 内核内部函数，不对用户开放。
 * 调用者必须处于临界区内，此函数不自行关中断。
 */
void rtos_sched_time_slice(struct rtos_tcb *tcb);
#endif

/* 空闲任务函数 */
void rtos_idle_task(void *param);

/* ============================================================
 * 🛡️ 断言（裸机环境无assert.h）
 * ============================================================ */

#define RTOS_ASSERT(cond)   \
    do {                    \
        if (!(cond)) {      \
            __asm volatile ("bkpt #0\n"); \
            for (;;);       \
        }                   \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* RTOS_KERNEL_H */
