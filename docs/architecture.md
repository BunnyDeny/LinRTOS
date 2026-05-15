# LinRTOS 架构详解

本文档从源码层面介绍 LinRTOS 的内核设计、调度算法、同步原语以及 Cortex-M 移植层的实现细节。

---

## 1. 内核对象与内存模型

LinRTOS 采用**全静态对象池**设计，内核运行期间不进行任何 `malloc`/`free` 操作，确保在裸机环境下的确定性行为。

| 对象类型 | 存储位置 | 配置宏 |
|----------|----------|--------|
| TCB (任务控制块) | `s_tcb_pool[RTOS_MAX_TASKS]` | `RTOS_MAX_TASKS` (默认 16) |
| 信号量 | `s_sem_pool[RTOS_MAX_SEMAPHORES]` | `RTOS_MAX_SEMAPHORES` (默认 16) |
| 互斥锁 | `s_mutex_pool[RTOS_MAX_MUTEXES]` | `RTOS_MAX_MUTEXES` (默认 16) |
| 软件定时器 | `s_timer_pool[RTOS_MAX_TIMERS]` | `RTOS_MAX_TIMERS` (默认 16) |
| 事件标志组 | `s_event_pool[RTOS_MAX_EVENTS]` | `RTOS_MAX_EVENTS` (默认 16) |
| 队列 | `s_queue_pool[RTOS_MAX_QUEUES]` | `RTOS_MAX_QUEUES` (默认 16) |

每个对象池通过位掩码（`uint32_t used_mask`）管理分配与回收，时间复杂度 O(1)。

---

## 2. 任务状态机

```
        create
    [ NONE ] ──────► [ READY ]
                        │
        ┌───────────────┼───────────────┐
        │               │               │
    yield/delay    take sem/mutex   delete
        │               │               │
        ▼               ▼               ▼
   [ BLOCKED ]    [ BLOCKED ]     [ DELETED ]
        │               │
   timeout/wakeup   give/unlock
        │               │
        └───────────────┘
                │
                ▼
            [ READY ]
```

- **READY**：任务在就绪队列中，等待调度器选中。
- **RUNNING**：当前正在 CPU 上执行的任务（由 `rtos_current_tcb` 指示）。
- **BLOCKED**：任务因延时、信号量、互斥锁、队列等阻塞，位于某个等待队列或延时队列中。
- **DELETED**：任务已被删除，TCB 等待回收。

---

## 3. 调度器设计

### 3.1 O(1) 优先级查找

调度器使用一个 **32-bit 就绪位图** `g_kernel.ready_map`：

```c
#if RTOS_MAX_PRIORITIES <= 32
    int prio = 31 - __builtin_clz(g_kernel.ready_map);
#endif
```

`__builtin_clz`（Count Leading Zeros）在一条指令内找到最高优先级，时间复杂度 O(1)。

### 3.2 同优先级轮转

当 `RTOS_ENABLE_TIME_SLICING` 使能时，每个任务拥有 `time_slice` 计数器。每次 SysTick 中断将其减一；归零时，任务被移到同优先级就绪队列尾部，若队列中还有其他任务则触发调度。

### 3.3 调度触发点

调度器在以下位置被显式调用：

1. `rtos_scheduler_start()` —— 启动首个任务。
2. `rtos_task_yield()` / `rtos_task_delay()` —— 任务主动放弃 CPU。
3. `rtos_sem_take()` / `rtos_mutex_take()` —— 任务阻塞。
4. `rtos_sem_give()` / `rtos_mutex_give()` / `rtos_task_resume()` —— 唤醒更高优先级任务。
5. `SysTick_Handler` (`rtos_tick_handler()`) —— tick 到期或时间片耗尽。

---

## 4. 上下文切换（Cortex-M Port）

### 4.1 栈布局

任务栈采用 **向下增长** 的满递减栈。初始化时，`rtos_port_init_stack()` 在栈顶构造两层结构：

```
高地址
┌─────────────────────────────┐ ◄── stack_top (8 字节对齐)
│  软件保存区：R4-R11 (32 B)   │
├─────────────────────────────┤ ◄── stack_ptr (返回给 TCB)
│  硬件异常帧：xPSR            │
│              PC (任务入口)   │
│              LR (exit 跳板)  │
│              R12             │
│              R3-R0           │
├─────────────────────────────┤
│          ... (任务运行期栈)  │
低地址
```

- **首次启动**：`SVC_Handler` 从 `rtos_current_tcb->stack_ptr` 恢复 `R4-R11`，设置 `PSP` 指向异常帧上方，然后 `BX 0xFFFFFFFD` 触发硬件自动弹出异常帧，任务入口函数获得执行权。
- **日常切换**：`PendSV_Handler` 保存当前 `R4-R11` 到当前 PSP，更新 `rtos_current_tcb->stack_ptr`；然后从 `rtos_next_tcb->stack_ptr` 恢复新任务的 `R4-R11`，更新 PSP，最后异常返回。

### 4.2 关键修复：current_task 同步

在 PendSV 执行前，`g_kernel.current_task` 与 `rtos_current_tcb` 是同步的；但 PendSV 会修改 `rtos_current_tcb`，而 `g_kernel.current_task` 不会自动更新。

LinRTOS 在 `PendSV_Handler` 中显式同步：

```asm
    ldr     r3, =g_kernel
    str     r2, [r3, #260]   @ g_kernel.current_task = rtos_next_tcb
```

这确保了 tick handler 和 `rtos_task_delay()` 等使用 `g_kernel.current_task` 的代码始终指向**真正正在运行**的任务，而非上一次调度的旧任务。

---

## 5. 同步原语

### 5.1 信号量

- **计数信号量**：内部维护 `count`，`give` 时若等待队列非空则唤醒最高优先级等待者，否则 `count++`。
- **超时机制**：`rtos_sem_take(timeout)` 将任务同时插入信号量等待队列和全局延时队列。tick handler 检查 `wake_tick`，超时后从两个队列中移除任务，并设置 `block_result = RTOS_ERR_TIMEOUT`。

### 5.2 互斥锁 + 优先级继承

- **持有者追踪**：`mutex->holder` 指向当前持有任务，`mutex->recursion` 支持递归加锁。
- **优先级继承**：当高优先级任务阻塞在互斥锁上时，若持有者的优先级较低，则临时提升持有者到阻塞者的优先级，并在就绪队列中重新排队。解锁时恢复持有者的 `base_priority`。
- **防止翻转**：继承后的持有者优先级高于任何中间优先级任务，杜绝经典优先级翻转问题。

### 5.3 延时队列

全局 `g_kernel.delay_list` 按 `wake_tick` 升序排列。tick handler 使用**有序遍历 + 提前退出**：一旦遇到未到期任务即可 `break`，避免扫描整个队列。

---

## 6. 软件定时器

- **回调上下文**：定时器回调在 `SysTick_Handler` 中直接执行（中断上下文），因此回调函数必须短小、不可阻塞。
- **周期模式**：`RTOS_TIMER_AUTO_RELOAD` 在每次到期后自动更新 `expire_tick += period`。
- **单次模式**：`RTOS_TIMER_ONE_SHOT` 到期后自动停止并从活跃列表移除。

---

## 7. 中断安全

- **临界区**：默认 `cpsid i` / `cpsie i`，支持嵌套（通过保存/恢复 PRIMASK）。
- **ISR 安全 API**：所有 `*_isr()` 后缀函数（如 `rtos_sem_give_isr()`）可在中断中调用，返回 `needs_switch` 标志，由应用决定是否在下一次中断退出时触发调度。
- **调度锁**：`g_kernel.sched_lock` 可临时禁止抢占，用于需要连续执行的关键代码段。

---

## 8. 性能数据（Cortex-M3 @ 80 MHz，编译优化 -O2）

| 指标 | 典型值 |
|------|--------|
| 上下文切换（PendSV） | ~ 12 个时钟周期（保存 + 恢复） |
| 任务创建 | ~ 200 个时钟周期 |
| 信号量 give/take（无竞争） | ~ 50 个时钟周期 |
| 互斥锁 lock/unlock（无继承） | ~ 60 个时钟周期 |
| 调度器查找（O(1)） | ~ 3 个时钟周期（`CLZ` 指令） |

---

## 9. 扩展方向

- **FPU 支持**：为 Cortex-M4F/M7 添加 `S16-S31` 的 Lazy Stacking 保存。
- **MPU 集成**：结合 Cortex-M MPU 实现任务栈溢出硬件保护。
- **低功耗模式**：在 `rtos_idle_task()` 中添加 `WFI` + 时钟门控钩子。
- **Trace 支持**：集成 ITM/SWO 输出任务切换事件，便于 SystemView 分析。
