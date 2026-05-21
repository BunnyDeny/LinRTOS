# LinRTOS 调度器源码详解

> 本文档从设计意图与执行流程两个维度，逐段拆解 `src/sched.c` 与 `src/tick.c` 的实现。

---

## 一、全局状态：内核的"中央档案室"

调度器维护着系统中所有任务的运行时状态，这些信息都集中在 `g_kernel` 这一个全局结构体中。

```c
struct rtos_kernel g_kernel;
volatile struct rtos_tcb *rtos_current_tcb = NULL;
volatile struct rtos_tcb *rtos_next_tcb = NULL;
```

- `g_kernel`：内核全局状态，包含所有就绪链表、延时队列、位图、当前任务指针等。
- `rtos_current_tcb`：**暴露给汇编代码**的全局指针。`PendSV_Handler` 在上下文切换时直接读写它，因此必须用 `volatile` 修饰。
- `rtos_next_tcb`：同样暴露给汇编，由 `rtos_sched()` 在触发 PendSV 前设置，指示切换目标。

这两个 `volatile` 全局变量是 C 代码与汇编异常处理程序之间的**通信接口**。

---

## 二、内核一次性初始化：`rtos_kernel_init()`

```c
static int s_kernel_inited = 0;

void rtos_kernel_init(void)
{
    if (s_kernel_inited) {
        return;
    }
    s_kernel_inited = 1;
    memset(&g_kernel, 0, sizeof(g_kernel));
    for (int i = 0; i < RTOS_MAX_PRIORITIES; i++) {
        rtos_list_init(&g_kernel.ready_list[i]);
    }
    rtos_list_init(&g_kernel.delay_list);
    rtos_list_init(&g_kernel.terminated_list);
}
```

**幂等设计**：`s_kernel_inited` 标志确保无论 `rtos_task_create()` 被调用多少次，内核结构只初始化一次。

**做了什么？**
1. `memset` 将整个 `g_kernel` 清零（`ready_map = 0`，所有指针为 `NULL`）。
2. 初始化每个优先级的就绪链表头节点。
3. 初始化延时队列和终止队列的链表头。

> 注意：`is_running` 在此时仍为 0，所以即使 SysTick 在 `rtos_port_init_systick()` 后就开始中断，`rtos_sched()` 也不会执行上下文切换——调度器还没"正式营业"。

---

## 三、就绪队列：位图 + 链表的双层结构

LinRTOS 的调度核心数据结构是一个**优先级数组 + 位图加速**的组合：

```
ready_map (位图)
    bit 7: 1  ← 优先级7有就绪任务
    bit 6: 0
    bit 5: 1  ← 优先级5有就绪任务
    ...
    bit 0: 1  ← 优先级0（空闲任务）

ready_list[7] → [TaskA] → [TaskB] → (head)
ready_list[5] → [TaskC] → (head)
```

### 3.1 加入就绪队列：`rtos_task_ready()`

```c
void rtos_task_ready(struct rtos_tcb *tcb)
{
    uint32_t prio = tcb->priority;
    rtos_list_insert_before(&g_kernel.ready_list[prio], &tcb->ready_node);
    g_kernel.ready_map |= ((rtos_ready_map_t)1 << prio);
    tcb->state = RTOS_TASK_READY;
}
```

**关键点**：
- 插入位置：`rtos_list_insert_before(head, node)` 实际上是**插入到链表尾部**（因为 LinRTOS 的链表头是循环双向链表，`head->prev` 指向最后一个节点）。
- 位图置位：无论该优先级下已有多少任务，位图对应的位始终为 1，表示"此优先级至少有一个就绪任务"。
- 状态切换：强制设为 `READY`，不关心之前是什么状态。

### 3.2 移出就绪队列：`rtos_task_unready()`

```c
void rtos_task_unready(struct rtos_tcb *tcb)
{
    uint32_t prio = tcb->priority;
    rtos_list_remove(&tcb->ready_node);
    if (rtos_list_is_empty(&g_kernel.ready_list[prio])) {
        g_kernel.ready_map &= ~((rtos_ready_map_t)1 << prio);
    }
    tcb->state = RTOS_TASK_BLOCKED;
}
```

**关键点**：
- 只有当该优先级的链表**完全变空**时，才将位图对应位清零。不能每移除一个任务就清零，否则同优先级的其他任务会丢失调度机会。
- 状态强制设为 `BLOCKED`。注意这里叫 `unready`，但状态设为 `BLOCKED` 而非 `SUSPENDED`，因为调用方（如 `rtos_task_delay()`）会在需要时再次修改状态。

---

## 四、O(1) 选最高优先级任务：`rtos_pick_highest_ready()`

这是调度器的**核心算法**，决定了"下一个该谁运行"。

```c
struct rtos_tcb *rtos_pick_highest_ready(void)
{
    if (g_kernel.ready_map == 0) {
        return g_kernel.idle_task;
    }

#if RTOS_MAX_PRIORITIES <= 32
    int prio = 31 - __builtin_clz((unsigned int)g_kernel.ready_map);
#else
    int prio;
    uint32_t high = (uint32_t)(g_kernel.ready_map >> 32);
    if (high) {
        prio = 63 - __builtin_clz(high);
    } else {
        uint32_t low = (uint32_t)g_kernel.ready_map;
        prio = 31 - __builtin_clz(low);
        prio = 63 - __builtin_clz(high);
    }
#endif

    struct rtos_list_node *node = g_kernel.ready_list[prio].next;
    return rtos_list_entry(node, struct rtos_tcb, ready_node);
}
```

### 算法原理：前导零计数（CLZ）

`__builtin_clz(x)` 返回整数 `x` 的二进制表示中**最高有效位前面的 0 的个数**。

```
ready_map = 0b0000 0000 0000 0000 0000 0000 1010 1000
                                    ↑
                                    最高置位是 bit 7
31 - __builtin_clz(ready_map) = 7
```

**为什么是 O(1)？**
- 不遍历、不扫描，一条 CPU 指令（`CLZ`）直接定位最高优先级。
- 32 位系统用 `uint32_t`，64 优先级系统用 `uint64_t` 分高低两半处理。
- 无论有多少任务、多少优先级，选最高优先级任务的时间恒定。

### 空闲任务兜底

```c
if (g_kernel.ready_map == 0) {
    return g_kernel.idle_task;
}
```

当所有用户任务都阻塞或挂起时，位图为 0，调度器退回空闲任务。这是系统不崩溃的最后保障。

---

## 五、调度核心：`rtos_sched()`

这是整个 RTOS 中最频繁被调用的函数之一。它的职责是：**判断是否需要切换任务，若需要则设置上下文切换**。

```c
void rtos_sched(void)
{
    if (!g_kernel.is_running || g_kernel.sched_lock > 0) {
        return;
    }

    /* 与汇编维护的 rtos_current_tcb 同步 */
    g_kernel.current_task = (struct rtos_tcb *)rtos_current_tcb;

    struct rtos_tcb *next = rtos_pick_highest_ready();
    struct rtos_tcb *curr = g_kernel.current_task;

    rtos_next_tcb = next;
    if (next != curr) {
        if (curr && curr->state != RTOS_TASK_DELETED) {
            curr->state = RTOS_TASK_READY;
        }
        next->state = RTOS_TASK_RUNNING;
        rtos_port_request_switch();
    }
}
```

### 执行流程拆解

1. **门卫检查**：
   - `!g_kernel.is_running`：调度器尚未启动（如正在创建第一个任务），直接返回。
   - `g_kernel.sched_lock > 0`：调度被锁（临界区嵌套），禁止抢占，直接返回。

2. **同步当前任务指针**：
   - `rtos_current_tcb` 在 `PendSV_Handler` 中被更新，而 `g_kernel.current_task` 是 C 代码视角的缓存。这里做一次同步，确保看到的是最新值。

3. **选出下一个任务**：
   - `rtos_pick_highest_ready()` 从位图 + 链表中找出最高优先级的就绪任务。

4. **判断是否需要切换**：
   - `next == curr`：当前任务已经是最高优先级，什么都不做。
   - `next != curr`：需要切换。

5. **状态转换**：
   - 当前任务（如果存在且未被删除）从 `RUNNING` 变回 `READY`。
   - 下一个任务设为 `RUNNING`。
   - `rtos_next_tcb = next`：给 `PendSV_Handler` 指明目标。
   - `rtos_port_request_switch()`：写 `ICSR.PENDSVSET`，触发 PendSV 异常。

### 为什么用 PendSV 而不是直接切换？

`rtos_port_request_switch()` 只设置一个标志位，真正的上下文切换在 **PendSV 异常** 中执行。这是 ARM Cortex-M 的惯用设计：
- PendSV 可以被配置为**最低优先级**。
- 如果 `rtos_sched()` 是在某个中断中被调用的，PendSV 会等到**所有高优先级中断处理完毕后**才执行。
- 避免了中断嵌套期间进行上下文切换导致的栈混乱。

---

## 六、调度请求：`rtos_sched_yield()`

```c
void rtos_sched_yield(void)
{
    RTOS_ENTER_CRITICAL();
    g_kernel.need_resched = 1;
    RTOS_EXIT_CRITICAL();
}
```

这是一个**轻量级标记函数**，只设置 `need_resched = 1`，并不立即触发切换。

**使用场景**：
- 在临界区内，调用方知道"等退出临界区后应该调度一次"，但不希望在中断关闭期间触发 PendSV。
- 配合 `rtos_sched_unlock()`：当调度锁降为 0 时，检查 `need_resched`，若为 1 则调用 `rtos_sched()`。

---

## 七、Tick 处理核心：`rtos_tick_handler()`

`tick.c` 只有一个主角：SysTick 中断中调用的 `rtos_tick_handler()`。它是 RTOS 的**心跳**，负责：
1. 递增全局 tick 计数。
2. 扫描并唤醒延时到期的任务。
3. 执行时间片轮转。

```c
void rtos_tick_handler(void)
{
    /* 若内核尚未初始化（如 HAL_Init 阶段 SysTick 已使能），直接返回 */
    if (g_kernel.ready_list[0].next == NULL) {
        return;
    }

    RTOS_ENTER_CRITICAL();

    g_kernel.tick_count++;

    /* ── 处理延时队列 ── */
    struct rtos_list_node *pos, *n;
    rtos_list_for_each_safe(pos, n, &g_kernel.delay_list) {
        struct rtos_tcb *tcb = rtos_list_entry(pos, struct rtos_tcb, delay_node);
        if ((int32_t)(g_kernel.tick_count - tcb->wake_tick) >= 0) {
            /* 延时到期，唤醒任务 */
            rtos_list_remove(&tcb->delay_node);
            tcb->wake_tick = 0;
            rtos_task_ready(tcb);
        } else {
            /* delay_list 按 wake_tick 升序排列，后面的更不会到期 */
            break;
        }
    }

    /* ── 时间片轮转 ── */
#if RTOS_ENABLE_TIME_SLICING
    struct rtos_tcb *curr = (struct rtos_tcb *)rtos_current_tcb;
    if (curr && curr != g_kernel.idle_task) {
        rtos_sched_time_slice(curr);
    }
#endif

    RTOS_EXIT_CRITICAL();

    /* 尝试调度（由 rtos_sched 内部决定是否需要上下文切换） */
    if (g_kernel.is_running && !g_kernel.sched_lock) {
        rtos_sched();
    }
}
```

### 7.1 防御性检查

```c
if (g_kernel.ready_list[0].next == NULL) {
    return;
}
```

在 HAL 或 BSP 初始化阶段，SysTick 可能已经被提前使能，但 RTOS 内核还没初始化。此时 `rtos_list_init()` 还没执行，链表指针是野值。这个检查避免了在`g_kernel` 尚未就绪时访问无效链表。

### 7.2 延时队列唤醒

```c
rtos_list_for_each_safe(pos, n, &g_kernel.delay_list) {
    struct rtos_tcb *tcb = rtos_list_entry(pos, struct rtos_tcb, delay_node);
    if ((int32_t)(g_kernel.tick_count - tcb->wake_tick) >= 0) {
        rtos_list_remove(&tcb->delay_node);
        tcb->wake_tick = 0;
        rtos_task_ready(tcb);
    } else {
        break;
    }
}
```

**有序队列的妙处**：`delay_list` 按 `wake_tick` 升序排列（由 `rtos_task_delay()` 维护）。一旦遇到第一个未到期任务，后面的任务 `wake_tick` 更大，肯定也没到期，直接 `break`。

**时间比较技巧**：
```c
(int32_t)(g_kernel.tick_count - tcb->wake_tick) >= 0
```
- 使用**有符号减法**正确处理 `uint32_t` 的回绕（wrap-around）。
- 即使 `tick_count` 从 `0xFFFFFFFF` 回绕到 `0`，这个表达式依然能正确判断"时间到了"。

### 7.3 退出临界区后的调度

```c
RTOS_EXIT_CRITICAL();

if (g_kernel.is_running && !g_kernel.sched_lock) {
    rtos_sched();
}
```

**刻意放在临界区外**：`rtos_sched()` 内部虽然也有保护，但最终会触发 `rtos_port_request_switch()`（设置 PendSV）。PendSV 的触发不需要关中断，而且放在临界区外可以减少中断延迟。

---

## 八、时间片轮转：`rtos_sched_time_slice()`

当 `RTOS_ENABLE_TIME_SLICING` 使能时，同优先级的任务之间可以按时间片轮流执行。

```c
#if RTOS_ENABLE_TIME_SLICING
void rtos_sched_time_slice(struct rtos_tcb *tcb)
{
    if (tcb->state != RTOS_TASK_RUNNING && tcb->state != RTOS_TASK_READY) {
        return;
    }
    if (tcb->time_slice > 0) {
        tcb->time_slice--;
        if (tcb->time_slice == 0) {
            /* 时间片耗尽，放到同优先级链表尾部 */
            tcb->time_slice = RTOS_TIME_SLICE_TICKS;
            rtos_list_remove(&tcb->ready_node);
            rtos_list_insert_before(&g_kernel.ready_list[tcb->priority],
                                    &tcb->ready_node);
            /* 如果同优先级还有其他任务，触发调度 */
            if (g_kernel.ready_list[tcb->priority].next != &tcb->ready_node) {
                g_kernel.need_resched = 1;
            }
        }
    }
}
#endif
```

### 执行流程

1. **状态过滤**：只有 `RUNNING` 或 `READY` 状态的任务才参与时间片轮转。被阻塞或挂起的任务不扣减时间片。

2. **扣减时间片**：每个 tick 减 1。

3. **时间片耗尽**：
   - 重置时间片为默认值 `RTOS_TIME_SLICE_TICKS`。
   - 把自己从就绪链表头部移到**尾部**。
   - `rtos_list_insert_before(head, node)` 即插入到尾部。

4. **是否需要调度？**
   ```c
   if (g_kernel.ready_list[tcb->priority].next != &tcb->ready_node)
   ```
   这个判断的含义是：移到自己后面的是不是还是我自己？
   - 如果 `next == &tcb->ready_node`，说明这个优先级下**只有一个任务**，移到尾部后前后都是自己，无需调度。
   - 如果 `next != &tcb->ready_node`，说明有**其他同优先级任务**，设置 `need_resched = 1`，提示调度器"该换人了"。

> 注意：`rtos_sched_time_slice()` 只设置 `need_resched = 1`，真正触发 `rtos_sched()` 是在 `rtos_tick_handler()` 末尾。

---

## 九、`SysTick_Handler`：弱定义的入口

```c
__attribute__((weak)) void SysTick_Handler(void)
{
    rtos_tick_handler();
}
```

- `weak` 属性允许用户在启动文件或其他地方提供自己的 `SysTick_Handler`。
- 如果用户没有覆盖，RTOS 自动接管 SysTick 中断。
- 用户自定义的 handler 中只需调用 `rtos_tick_handler()` 即可与 RTOS 兼容。

---

## 十、调度器的设计哲学总结

| 设计点 | 说明 |
|--------|------|
| **O(1) 优先级查找** | `CLZ` 指令 + 位图，无论优先级数量和任务数量，选最高优先级任务恒定时长。 |
| **位图 + 链表双层结构** | 位图快速判断"哪些优先级有任务"，链表管理同优先级下的多个任务（FIFO）。 |
| **PendSV 延迟切换** | `rtos_sched()` 只标记 PendSV，在最低优先级异常中执行实际上下文切换，避免中断嵌套期间栈混乱。 |
| **临界区最小化** | `rtos_tick_handler()` 把 `rtos_sched()` 放在 `RTOS_EXIT_CRITICAL()` 之后，缩短中断关闭时间。 |
| **有序延时队列** | `delay_list` 按 `wake_tick` 升序排列，tick handler 遇到未到期即可提前终止扫描。 |
| **时间片条件触发** | 同优先级只有一个任务时不触发无意义的上下文切换。 |
| **volatile 汇编接口** | `rtos_current_tcb` / `rtos_next_tcb` 作为 C 与 PendSV_Handler 的通信桥梁。 |
| **防御性编程** | `ready_list[0].next == NULL` 检查避免未初始化时访问野指针；`rtos_pick_highest_ready()` 的位图为空检查兜底到空闲任务。 |

---

> 读完本文档后，你已经理解了 LinRTOS 的调度骨架：位图加速的优先级调度、PendSV 驱动的上下文切换、以及 SysTick 驱动的延时唤醒与时间片轮转。下一步建议阅读 `port/` 目录下的汇编实现，看看 `PendSV_Handler` 如何用十几行汇编完成真正的寄存器保存与恢复。
