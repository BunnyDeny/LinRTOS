# LinRTOS 架构详解

本文档从源码层面介绍 LinRTOS 的内核设计、调度算法以及 Cortex-M 移植层的实现细节。

---

## 1. 内核对象与内存模型

LinRTOS 采用**全静态对象池**设计，内核运行期间不进行任何 `malloc`/`free` 操作，确保在裸机环境下的确定性行为。

| 对象类型 | 存储位置 | 配置宏 |
|----------|----------|--------|
| TCB (任务控制块) | `s_tcb_pool[RTOS_MAX_TASKS]` | `RTOS_MAX_TASKS` (默认 16) |

每个对象池通过位掩码（`uint32_t used_mask`）管理分配与回收，时间复杂度 O(1)。

---

## 2. 任务状态机

```
        create
    [ NONE ] ──────► [ READY ]
                        │
        ┌───────────────┼───────────────┐
        │               │               │
    yield/delay    suspend          delete
        │               │               │
        ▼               ▼               ▼
   [ BLOCKED ]    [ SUSPENDED ]    [ DELETED ]
        │               │
   timeout/wakeup   resume
        │               │
        └───────────────┘
                │
                ▼
            [ READY ]
```

- **READY**：任务在就绪队列中，等待调度器选中。
- **RUNNING**：当前正在 CPU 上执行的任务（由 `rtos_current_tcb` 指示）。
- **BLOCKED**：任务因延时阻塞，位于延时队列中。
- **SUSPENDED**：任务被挂起，不参与调度。
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
3. `rtos_task_suspend()` —— 任务挂起。
4. `rtos_task_resume()` —— 恢复更高优先级任务。
5. `SysTick_Handler` (`rtos_tick_handler()`) —— tick 到期或时间片耗尽。
6. `rtos_task_set_priority()` —— 优先级变更后重新排队。

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

### 4.2 关键同步

在 PendSV 执行前，`g_kernel.current_task` 与 `rtos_current_tcb` 是同步的；但 PendSV 会修改 `rtos_current_tcb`，而 `g_kernel.current_task` 不会自动更新。

LinRTOS 在 `PendSV_Handler` 中显式同步：

```asm
    ldr     r3, =g_kernel
    str     r2, [r3, #260]   @ g_kernel.current_task = rtos_next_tcb
```

这确保了 tick handler 和 `rtos_task_delay()` 等使用 `g_kernel.current_task` 的代码始终指向**真正正在运行**的任务。

---

## 5. 延时队列

全局 `g_kernel.delay_list` 按 `wake_tick` 升序排列。tick handler 使用**有序遍历 + 提前退出**：一旦遇到未到期任务即可 `break`，避免扫描整个队列。

---

## 6. 中断安全

- **临界区**：默认 `cpsid i` / `cpsie i`，支持嵌套（通过保存/恢复 PRIMASK）。
- **调度锁**：`g_kernel.sched_lock` 可临时禁止抢占，用于需要连续执行的关键代码段。

---

## 7. 性能数据（Cortex-M3 @ 80 MHz，编译优化 -O2）

| 指标 | 典型值 |
|------|--------|
| 上下文切换（PendSV） | ~ 12 个时钟周期（保存 + 恢复） |
| 任务创建 | ~ 200 个时钟周期 |
| 调度器查找（O(1)） | ~ 3 个时钟周期（`CLZ` 指令） |

---

## 8. 扩展方向

- **FPU 支持**：为 Cortex-M4F/M7 添加 `S16-S31` 的 Lazy Stacking 保存。
- **MPU 集成**：结合 Cortex-M MPU 实现任务栈溢出硬件保护。
- **低功耗模式**：在 `rtos_idle_task()` 中添加 `WFI` + 时钟门控钩子。
- **Trace 支持**：集成 ITM/SWO 输出任务切换事件，便于 SystemView 分析。
