# LinRTOS `task.c` 源码详解

> 本文档从设计意图与执行流程两个维度，逐段拆解 `src/task.c` 的实现。

---

## 一、任务的"原材料"：TCB 静态池

LinRTOS 奉行**零动态内存**的设计哲学。内核启动前就预分配好了所有任务控制块（TCB），运行时不再调用 `malloc`/`free`。

```c
static struct rtos_tcb s_tcb_pool[RTOS_MAX_TASKS];
static uint32_t s_tcb_used_mask = 0;
```

- `s_tcb_pool`：静态 TCB 数组，大小由配置 `RTOS_MAX_TASKS` 决定（默认 16）。
- `s_tcb_used_mask`：位图，第 `i` 位为 1 表示 `s_tcb_pool[i]` 已被占用。

分配与释放的实现极为朴素：

```c
static struct rtos_tcb *rtos_tcb_alloc(void)
{
    RTOS_ENTER_CRITICAL();
    for (int i = 0; i < RTOS_MAX_TASKS; i++) {
        if (!(s_tcb_used_mask & (1U << i))) {
            s_tcb_used_mask |= (1U << i);
            struct rtos_tcb *tcb = &s_tcb_pool[i];
            memset(tcb, 0, sizeof(*tcb));
            rtos_list_init(&tcb->ready_node);
            rtos_list_init(&tcb->delay_node);
            RTOS_EXIT_CRITICAL();
            return tcb;
        }
    }
    RTOS_EXIT_CRITICAL();
    return NULL;
}
```

**关键点**：
- 遍历查找的时间复杂度是 O(N)，但 N 最大只有 16（或 64），代价可以忽略。
- `RTOS_ENTER_CRITICAL()` 是必须的——`rtos_task_create()` 可能在任务运行时被另一个任务或中断上下文调用，必须保护位图操作的原子性。
- `memset` 清零后再初始化链表节点，确保 TCB 处于干净状态。

---

## 二、空闲任务：调度器的"保底"

```c
static uint32_t s_idle_stack[RTOS_IDLE_STACK_SIZE];
static struct rtos_tcb s_idle_tcb;

void rtos_idle_task(void *param)
{
    (void)param;
    for (;;) {
        __asm volatile ("wfi");
    }
}
```

空闲任务是调度器的**最后一道防线**。当所有用户任务都阻塞或挂起时，调度器必须有一个任务可运行，否则 CPU 将无处可去。

空闲任务的特点：
- 优先级固定为 **0**（最低）。
- 栈空间也是静态数组，不经过 TCB 池分配。
- 只做一件事：`wfi`（Wait For Interrupt），让 CPU 进入低功耗休眠，直到下一个中断唤醒。

`sched.c` 中的 `rtos_pick_highest_ready()` 有一行兜底逻辑：

```c
if (g_kernel.ready_map == 0) {
    return g_kernel.idle_task;
}
```

如果没有空闲任务，且所有用户任务都阻塞了，`ready_map == 0`，调度器将无处可去，系统崩溃。

---

## 三、任务的出生：`rtos_task_create()`

这是用户最常调用的 API。它的完整流程如下：

### 3.1 确保内核已初始化

```c
rtos_kernel_init();
```

`rtos_kernel_init()` 定义在 `sched.c` 中，内部有 `s_kernel_inited` 标志保证**幂等**——无论你调用多少次 `rtos_task_create()`，内核全局结构只会初始化一次。

### 3.2 参数检查

```c
if (!func || !stack_buffer || stack_depth_words < 32) {
    return RTOS_ERR_PARAM;
}
if (priority >= RTOS_MAX_PRIORITIES) {
    return RTOS_ERR_PARAM;
}
```

- `stack_depth_words < 32`：栈太小（少于 128 字节），直接拒绝。
- 优先级必须在 `[0, RTOS_MAX_PRIORITIES)` 范围内。

### 3.3 申请 TCB

```c
struct rtos_tcb *tcb = rtos_tcb_alloc();
if (!tcb) {
    return RTOS_ERR_NOMEM;
}
```

### 3.4 填充 TCB

```c
strncpy(tcb->name, name ? name : "", RTOS_MAX_TASK_NAME_LEN - 1);
tcb->name[RTOS_MAX_TASK_NAME_LEN - 1] = '\0';

tcb->stack_base = stack_buffer;                    // 低地址（数组开头）
tcb->stack_size = stack_depth_words;               // 栈大小（以字为单位）
tcb->stack_top  = stack_buffer + stack_depth_words; // 高地址（数组末尾）
tcb->priority   = priority;
tcb->time_slice = RTOS_TIME_SLICE_TICKS;           // 默认时间片
tcb->state      = RTOS_TASK_READY;                 // 出生即为就绪态
```

**栈指针的命名与方向**（Cortex-M 为满递减栈）：

```
高地址（数组末尾）
stack_top ─────────────────────┐
    │                          │
    │  栈向低地址方向增长        │
    │                          │
    ▼                          │
stack_base ────────────────────┘
低地址（数组开头）
```

- `stack_base`：栈的最低地址。
- `stack_top`：栈的最高地址，`rtos_port_init_stack()` 从这里往下"画"初始栈帧。
- `stack_ptr`：当前栈指针，运行期动态变化，由 TCB 保存。

### 3.5 栈初始化

```c
rtos_stack_fill(stack_buffer, stack_depth_words);
tcb->stack_ptr = rtos_port_init_stack(tcb->stack_top, func, param);
```

**`rtos_stack_fill`**：用魔数 `0xA5A5A5A5` 填满整个栈数组。后续 `rtos_task_get_stack_free()` 通过扫描栈底还剩多少魔数，来判断栈使用了多少。

**`rtos_port_init_stack`**：port 层的核心函数。它从高地址往低地址伪造初始栈帧：

```
高地址（栈底）
┌─────────────────┐
│ xPSR = 0x01000000 │  ← Thumb 位必须置 1
│ PC   = func        │  ← 任务入口地址
│ LR   = exit_tramp  │  ← 兜底：任务 return 时自动删除自身
│ R12  = 0           │
│ R3-R1 = 0          │
│ R0   = param       │  ← 任务函数收到的参数
├─────────────────┤  ← 硬件自动保存/恢复区（8 个寄存器，32 字节）
│ R11-R4 = 0         │
│ EXC_RETURN         │  ← 0xFFFFFFFD（基本帧）
└─────────────────┘ ← stack_ptr（返回值，存入 TCB）
低地址（栈顶）
```

函数返回的 `stack_ptr` 指向 **R4 的位置**——这正是 `SVC_Handler` 或 `PendSV_Handler` 开始恢复时读的第一个地址。

### 3.6 加入就绪队列

```c
RTOS_ENTER_CRITICAL();
rtos_task_ready(tcb);
RTOS_EXIT_CRITICAL();
```

`rtos_task_ready()` 在 `sched.c` 中实现，负责把任务插入对应优先级的就绪链表，并在就绪位图中标记该优先级有任务可运行。

### 3.7 抢占检查

```c
if (g_kernel.is_running && tcb->priority > g_kernel.current_task->priority) {
    rtos_sched();
}
```

**抢占式调度的核心体现**。如果调度器已经启动，且新任务的优先级高于当前运行任务，立即触发 `rtos_sched()`，通过 PendSV 切换到新任务。

---

## 四、任务的"主动放弃"：`rtos_task_delay()`

```c
void rtos_task_delay(uint32_t ticks)
{
    if (ticks == 0) {
        rtos_task_yield();
        return;
    }

    struct rtos_tcb *tcb = g_kernel.current_task;

    RTOS_ENTER_CRITICAL();
    tcb->wake_tick = g_kernel.tick_count + ticks;
    tcb->state = RTOS_TASK_BLOCKED;
    rtos_task_unready(tcb);

    /* 按 wake_tick 升序插入延时队列 */
    struct rtos_list_node *pos;
    rtos_list_for_each(pos, &g_kernel.delay_list) {
        struct rtos_tcb *p = rtos_list_entry(pos, struct rtos_tcb, delay_node);
        if ((int32_t)(p->wake_tick - tcb->wake_tick) > 0) {
            break;
        }
    }
    rtos_list_insert_before(pos, &tcb->delay_node);

    RTOS_EXIT_CRITICAL();
    rtos_sched();
}
```

### 做了什么？

1. **计算唤醒时间**：`wake_tick = 当前 tick + 要延时的 tick`。保存的是**绝对时间**。
2. **状态转移**：从 `READY` 变为 `BLOCKED`。
3. **移出就绪队列**：`rtos_task_unready()` 把任务从就绪链表摘掉，更新位图。
4. **插入延时队列**：按 `wake_tick` 从小到大排队。

**关键的比较技巧**：

```c
if ((int32_t)(p->wake_tick - tcb->wake_tick) > 0)
```

使用**有符号减法**处理 `uint32_t` 的 tick 回绕（wrap-around）。这是嵌入式系统中比较时间戳的标准写法。

5. **触发调度**：`rtos_sched()` 选出下一个最高优先级任务，发起 PendSV 切换。

---

## 五、绝对周期延时：`rtos_task_delay_until()`

```c
void rtos_task_delay_until(uint32_t *prev_wake_tick, uint32_t interval)
{
    if (!prev_wake_tick || interval == 0) {
        return;
    }

    uint32_t next_wake = *prev_wake_tick + interval;
    uint32_t now = rtos_get_tick_count();

    if ((int32_t)(next_wake - now) > 0) {
        rtos_task_delay(next_wake - now);
    }

    *prev_wake_tick = next_wake;
}
```

这是为**周期性任务**设计的 API。例如一个控制任务必须严格每 10 ms 执行一次：

```c
static uint32_t prev = 0;
for (;;) {
    rtos_task_delay_until(&prev, 10);  // 严格按 10 tick 周期对齐
    do_control();
}
```

**与 `rtos_task_delay()` 的区别**：
- `rtos_task_delay(10)` 是"从现在起睡 10 tick"，会累积误差。
- `rtos_task_delay_until(&prev, 10)` 是"睡到上次该醒的时间 + 10 tick"，即使本次被高优先级任务抢占了而迟到，下次仍然按原始周期对齐，**不累积漂移**。

---

## 六、主动让出：`rtos_task_yield()`

```c
void rtos_task_yield(void)
{
    RTOS_ENTER_CRITICAL();
#if RTOS_ENABLE_TIME_SLICING
    struct rtos_tcb *tcb = g_kernel.current_task;
    tcb->time_slice = RTOS_TIME_SLICE_TICKS;
    rtos_list_remove(&tcb->ready_node);
    rtos_list_insert_before(&g_kernel.ready_list[tcb->priority],
                            &tcb->ready_node);
#endif
    g_kernel.need_resched = 1;
    RTOS_EXIT_CRITICAL();
    rtos_sched();
}
```

- 如果使能了时间片轮转，把自己移到同优先级就绪链表的**尾部**，并重置时间片。
- 设置 `need_resched = 1`，触发调度。

`rtos_task_delay(0)` 也会调到这个函数，效果就是：当前任务不进入阻塞，但主动放弃 CPU，让同优先级的其他任务有机会运行。

---

## 七、任务的"封印"：挂起与恢复

### 挂起

```c
void rtos_task_suspend(rtos_task_handle_t task)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)task;
    if (!tcb) {
        tcb = g_kernel.current_task;  // NULL = 挂起自己
    }

    RTOS_ENTER_CRITICAL();
    if (tcb->state == RTOS_TASK_READY || tcb->state == RTOS_TASK_RUNNING) {
        rtos_task_unready(tcb);
        tcb->state = RTOS_TASK_SUSPENDED;
        if (tcb == g_kernel.current_task) {
            RTOS_EXIT_CRITICAL();
            rtos_sched();  // 自己挂起自己，必须切走
            return;
        }
    }
    RTOS_EXIT_CRITICAL();
}
```

**挂起 = 从调度器眼中消失**。被挂起的任务不参与任何调度，也不在延时队列里，它就在 `SUSPENDED` 状态等着别人来唤醒。

### 恢复

```c
void rtos_task_resume(rtos_task_handle_t task)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)task;
    if (!tcb) return;

    RTOS_ENTER_CRITICAL();
    if (tcb->state == RTOS_TASK_SUSPENDED) {
        rtos_task_ready(tcb);
        if (tcb->priority > g_kernel.current_task->priority) {
            RTOS_EXIT_CRITICAL();
            rtos_sched();  // 高优先级任务被唤醒，立即抢占
            return;
        }
    }
    RTOS_EXIT_CRITICAL();
}
```

**恢复 = 重新加入就绪队列**。如果被恢复的任务优先级高于当前任务，立即触发抢占式调度。

---

## 八、任务的"死亡"：`rtos_task_delete()`

```c
void rtos_task_delete(rtos_task_handle_t task)
{
    struct rtos_tcb *tcb = (struct rtos_tcb *)task;

    if (!tcb) {
        tcb = g_kernel.current_task;  // NULL = 删除自己
    }

    RTOS_ENTER_CRITICAL();

    if (tcb->state == RTOS_TASK_READY || tcb->state == RTOS_TASK_RUNNING) {
        rtos_task_unready(tcb);
    } else if (tcb->state == RTOS_TASK_BLOCKED) {
        rtos_list_remove(&tcb->delay_node);
    }

    tcb->state = RTOS_TASK_DELETED;

    if (tcb == g_kernel.current_task) {
        /* 自删：触发调度，永不返回 */
        g_kernel.need_resched = 1;
        g_kernel.sched_lock = 0;
        RTOS_EXIT_CRITICAL();
        rtos_sched();
        /* 理论上不会执行到这里 */
        for (;;)
            __asm volatile ("wfi");
    }

    rtos_tcb_free(tcb);
    RTOS_EXIT_CRITICAL();
}
```

### 删别人

从就绪队列或延时队列移除，标记为 `DELETED`，释放 TCB。干净利落。

### 删自己（自删）

这是难点。当前任务还在运行，但它要把自己删掉：

1. 设置 `need_resched = 1`，解锁调度器。
2. 调用 `rtos_sched()` 触发切换。
3. 当前任务不会再得到 CPU。
4. `rtos_sched()` 之后的死循环是防御性编程——理论上永远不会执行到。

> **注意**：自删后，当前任务的 TCB 和栈空间**没有被释放**，因为代码在 `rtos_sched()` 后就进入了死循环，没有机会执行 `rtos_tcb_free()`。这是许多 RTOS 的通用简化做法：自删资源不回收，留到系统重启前处理。

---

## 九、调度器启动：`rtos_scheduler_start()`

```c
void rtos_scheduler_start(void)
{
    rtos_port_init();                           // ① 设置 PendSV/SysTick 优先级
    rtos_kernel_init();                         // ② 初始化内核全局结构
    rtos_create_idle_task();                    // ③ 创建空闲任务
    rtos_port_init_systick(RTOS_TICK_RATE_HZ);  // ④ 启动 SysTick 定时器
    g_kernel.current_task = rtos_pick_highest_ready();  // ⑤ 选出首个任务
    g_kernel.current_task->state = RTOS_TASK_RUNNING;
    g_kernel.is_running = 1;
    rtos_current_tcb = g_kernel.current_task;   // ⑥ 同步给汇编代码
    rtos_port_start_first_task();               // ⑦ 触发 SVC，永不返回
    for (;;) {
        __asm volatile ("wfi");
    }
}
```

结合 `port` 层的知识，这个流程完全贯通了：

1. `rtos_port_init()`：把 PendSV 和 SysTick 设为最低优先级，防止 MSP 栈帧嵌套灾难。
2. `rtos_create_idle_task()`：保底任务，确保就绪队列永远不会空。
3. `rtos_pick_highest_ready()`：从位图 + 链表里找出优先级最高的就绪任务。
4. `rtos_current_tcb = ...`：把这个全局指针暴露给汇编代码（`SVC_Handler` 要读它）。
5. `rtos_port_start_first_task()`：执行 `svc #0`，进入 `SVC_Handler`：
   - 设置 `CONTROL.SPSEL = 1`（Thread 模式以后用 PSP）。
   - 从 `rtos_current_tcb->stack_ptr` 恢复 R4-R11。
   - 更新 PSP，执行 `bx lr`（EXC_RETURN）。
   - 硬件自动从 PSP 弹出异常帧（R0-R3, R12, LR, PC, xPSR）。
   - **第一个任务开始运行，调度器正式启动**。

---

## 十、`task.c` 的设计哲学总结

| 设计点 | 说明 |
|--------|------|
| **静态 TCB 池** | 零 `malloc`，确定性行为，适合裸机固件。 |
| **用户供栈** | `rtos_task_create()` 要求传入 `stack_buffer`，栈大小由用户决定，内存布局完全可控。 |
| **状态机驱动** | `READY → RUNNING → BLOCKED → SUSPENDED → DELETED`，每个状态转换都有明确的队列操作。 |
| **延时队列有序插入** | 按 `wake_tick` 升序排列，SysTick handler 遇到未到期任务即可提前 `break`，避免全量扫描。 |
| **创建时抢占** | 高优先级任务创建后立即 `rtos_sched()`，体现抢占式调度的即时性。 |
| **自删/自挂起特殊处理** | 当前任务删除或挂起自己时，必须触发调度切走，否则 CPU 将无处可去。 |
| **栈溢出检测** | 魔数 `0xA5A5A5A5` 填充栈底，`rtos_task_get_stack_free()` 扫描剩余魔数。 |
